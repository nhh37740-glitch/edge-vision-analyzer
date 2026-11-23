#include "streamer/rtsp_pusher.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace edge {
namespace {

#ifndef _WIN32

class PosixProcessSink final : public ProcessSink {
 public:
  ~PosixProcessSink() override {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    if (pid_ > 0) {
      int status = 0;
      while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }

  bool Start(const std::vector<std::string>& command,
             std::string* error) override {
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
      if (error != nullptr) {
        *error = std::string("pipe failed: ") + std::strerror(errno);
      }
      return false;
    }

    // Prepare argv before fork so the child only calls async-signal-safe
    // functions between fork and exec.
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const std::string& arg : command) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
      if (error != nullptr) {
        *error = std::string("fork failed: ") + std::strerror(errno);
      }
      ::close(pipe_fds[0]);
      ::close(pipe_fds[1]);
      return false;
    }

    if (pid == 0) {
      ::dup2(pipe_fds[0], STDIN_FILENO);
      ::close(pipe_fds[0]);
      ::close(pipe_fds[1]);
      ::execvp(argv[0], argv.data());
      ::_exit(127);
    }

    ::close(pipe_fds[0]);
    const int flags = ::fcntl(pipe_fds[1], F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK);
    }
    fd_ = pipe_fds[1];
    pid_ = pid;
    return true;
  }

  std::ptrdiff_t Write(const std::uint8_t* data, std::size_t size) override {
    if (fd_ < 0) {
      return -1;
    }
    const ssize_t written = ::write(fd_, data, size);
    if (written < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return 0;
      }
      return -1;
    }
    return written;
  }

  void CloseInput() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int Wait() override {
    if (pid_ <= 0) {
      return -1;
    }
    int status = 0;
    pid_t result;
    do {
      result = ::waitpid(pid_, &status, 0);
    } while (result < 0 && errno == EINTR);
    pid_ = -1;
    return result < 0 ? -1 : status;
  }

 private:
  int fd_ = -1;
  pid_t pid_ = -1;
};

#else  // _WIN32

// Windows tests inject a fake sink; this production fallback exists only for
// local builds. Writing uses a binary-mode pipe so rawvideo bytes are not
// translated. It is blocking, which is acceptable because the target runtime
// is Linux and writes happen on the bounded writer thread.
std::string QuoteWindowsArg(const std::string& arg) {
  if (arg.find_first_of(" \t\"") == std::string::npos) {
    return arg;
  }
  std::string quoted = "\"";
  for (const char ch : arg) {
    if (ch == '"') {
      quoted += "\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += '"';
  return quoted;
}

class PopenProcessSink final : public ProcessSink {
 public:
  ~PopenProcessSink() override {
    if (pipe_ != nullptr) {
      ::_pclose(pipe_);
    }
  }

  bool Start(const std::vector<std::string>& command,
             std::string* error) override {
    std::string line;
    for (std::size_t i = 0; i < command.size(); ++i) {
      if (i != 0) {
        line += ' ';
      }
      line += QuoteWindowsArg(command[i]);
    }
    pipe_ = ::_popen(line.c_str(), "wb");
    if (pipe_ == nullptr) {
      if (error != nullptr) {
        *error = "failed to launch: " + line;
      }
      return false;
    }
    return true;
  }

  std::ptrdiff_t Write(const std::uint8_t* data, std::size_t size) override {
    if (pipe_ == nullptr) {
      return -1;
    }
    const std::size_t written = std::fwrite(data, 1, size, pipe_);
    if (written < size && std::ferror(pipe_) != 0) {
      return -1;
    }
    return static_cast<std::ptrdiff_t>(written);
  }

  void CloseInput() override {
    if (pipe_ != nullptr) {
      std::fflush(pipe_);
    }
  }

  int Wait() override {
    if (pipe_ == nullptr) {
      return -1;
    }
    const int status = ::_pclose(pipe_);
    pipe_ = nullptr;
    return status;
  }

 private:
  std::FILE* pipe_ = nullptr;
};

#endif  // _WIN32

}  // namespace

std::shared_ptr<ProcessSink> make_default_process_sink() {
#ifdef _WIN32
  return std::make_shared<PopenProcessSink>();
#else
  return std::make_shared<PosixProcessSink>();
#endif
}

RtspPusher::RtspPusher(std::string mode, std::string rtsp_url, int width,
                       int height, int fps, std::string ffmpeg_path,
                       std::shared_ptr<ProcessSink> sink)
    : mode_(std::move(mode)),
      rtsp_url_(std::move(rtsp_url)),
      ffmpeg_path_(std::move(ffmpeg_path)),
      width_(width),
      height_(height),
      fps_(fps),
      injected_sink_(std::move(sink)) {}

RtspPusher::~RtspPusher() { stop(); }

std::vector<std::string> RtspPusher::build_command(
    const std::string& ffmpeg_path, int width, int height, int fps,
    const std::string& rtsp_url) {
  return {ffmpeg_path,
          "-f",
          "rawvideo",
          "-pix_fmt",
          "rgb24",
          "-s",
          std::to_string(width) + "x" + std::to_string(height),
          "-r",
          std::to_string(fps),
          "-i",
          "-",
          "-c:v",
          "libx264",
          "-preset",
          "ultrafast",
          "-tune",
          "zerolatency",
          "-b:v",
          "500k",
          "-f",
          "rtsp",
          rtsp_url};
}

bool RtspPusher::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_.load()) {
    return true;
  }
  if (mode_ != "rtsp") {
    last_error_ = "streamer mode is not rtsp";
    return false;
  }
  if (rtsp_url_.empty()) {
    last_error_ = "rtsp_url is empty";
    return false;
  }
  if (width_ <= 0 || height_ <= 0 || fps_ <= 0) {
    last_error_ = "invalid frame geometry";
    return false;
  }

  std::shared_ptr<ProcessSink> sink =
      injected_sink_ != nullptr ? injected_sink_ : make_default_process_sink();
  if (sink == nullptr) {
    last_error_ = "no process sink available";
    return false;
  }

  std::string error;
  if (!sink->Start(build_command(ffmpeg_path_, width_, height_, fps_,
                                 rtsp_url_),
                   &error)) {
    last_error_ = error.empty() ? "failed to launch ffmpeg" : error;
    return false;
  }

  sink_ = std::move(sink);
  queue_.clear();
  stopping_.store(false);
  running_.store(true);
  last_error_.clear();
  writer_ = std::thread(&RtspPusher::writer_loop, this);
  return true;
}

void RtspPusher::push(const FrameMeta& frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_.load()) {
    return;
  }
  const std::size_t expected = static_cast<std::size_t>(width_) *
                               static_cast<std::size_t>(height_) * 3u;
  if (frame.rgb_data.size() != expected) {
    ++dropped_;
    return;
  }
  if (queue_.size() >= kMaxQueuedFrames) {
    ++dropped_;
    return;
  }
  queue_.emplace_back(frame.rgb_data);
  cv_.notify_one();
}

void RtspPusher::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_.store(true);
    running_.store(false);
  }
  cv_.notify_all();

  if (writer_.joinable()) {
    writer_.join();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
  if (sink_ != nullptr) {
    sink_->CloseInput();
    sink_->Wait();
    sink_.reset();
  }
}

std::string RtspPusher::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

void RtspPusher::writer_loop() {
  for (;;) {
    std::vector<std::uint8_t> frame;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] {
        return stopping_.load() || !queue_.empty();
      });
      if (queue_.empty()) {
        if (stopping_.load()) {
          return;
        }
        continue;
      }
      frame = std::move(queue_.front());
      queue_.pop_front();
    }
    if (!write_frame(frame)) {
      return;
    }
  }
}

bool RtspPusher::write_frame(const std::vector<std::uint8_t>& data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    if (stopping_.load()) {
      return false;
    }
    const std::ptrdiff_t written =
        sink_->Write(data.data() + offset, data.size() - offset);
    if (written < 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = "ffmpeg input pipe closed";
      running_.store(false);
      return false;
    }
    if (written == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

}  // namespace edge
