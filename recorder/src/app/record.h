#pragma once

bool record();
bool playWavFile(const char* path, bool* cancelled = nullptr);
bool playRecording(int noteNum);
int  recordingPartCount(int noteNum);
uint32_t recordingDurationSeconds(int noteNum);
