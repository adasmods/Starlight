#pragma once

#include <filesystem>
#include <string>

// Screen + game audio recorder.
// Video: the back buffer is read every frame (constant FPS, frames duplicated or
// skipped to match real time) and piped to ffmpeg on a worker thread.
// Audio: a passthrough FMOD DSP on the master group captures the final mix,
// which is muxed into the video when recording stops.
namespace starlight::recorder {

bool isRecording();
bool isFinishing(); // still encoding / muxing after stop
bool start(std::string& message);
void stop();
void toggle();

// called right before the frame is presented (CCEGLView::swapBuffers)
void captureFrame();

std::string status();
std::string findFfmpeg();
std::filesystem::path videoDir();

} // namespace starlight::recorder
