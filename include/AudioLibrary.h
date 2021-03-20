#include <alsa/asoundlib.h>
#include <iostream>
#include <cmath>
#include <fstream>
#include <cstring>
#include <functional>

namespace ral {

    class AudioFile {
    public:
        AudioFile(const char* filename) : m_channels(0), m_sampleRate(0), m_bitsPerSample(0), m_dataSize(0), m_frames(0), m_pcmData(nullptr) {
            std::ifstream file(filename, std::ios::in | std::ios::binary | std::ios::ate);

            if(!file.is_open()) {
                std::cout << "Could not open file " << filename << std::endl;
                return;
            }

            std::streampos fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            uint8_t* fileBuffer = new uint8_t[fileSize];
            file.read((char*)fileBuffer, fileSize);
            
            file.close();

            parseWavFile(fileBuffer, fileSize);

            delete[] fileBuffer;
        }

        unsigned int getChannels() const { return m_channels; }
        unsigned int getSampleRate() const { return m_sampleRate; }
        unsigned int getBitsPerSample() const { return m_bitsPerSample; }
        unsigned int getDataSize() const { return m_dataSize; }
        unsigned int getFrames() const { return m_frames; }
        uint8_t* const getPcmData() const { return m_pcmData; }

    private:
        void parseWavFile(uint8_t* fileBuffer, unsigned int fileBufferSize) {
            // Header
            if(fileBufferSize < 44) {
                std::cout << "File too small" << std::endl;
                return;
            }

            if(fileBuffer[0] != 'R' || fileBuffer[1] != 'I' || fileBuffer[2] != 'F' || fileBuffer[3] != 'F') {
                std::cout << "Not a wav file" << std::endl;
                return;
            }

            if(fileBuffer[8] != 'W' || fileBuffer[9] != 'A' || fileBuffer[10] != 'V' || fileBuffer[11] != 'E') {
                std::cout << "Not a wav file" << std::endl;
                return;
            }

            if(fileBuffer[12] != 'f' || fileBuffer[13] != 'm' || fileBuffer[14] != 't' || fileBuffer[15] != ' ') {
                std::cout << "Not fmt chunk" << std::endl;
                return;
            }

            uint32_t subchunk1Size = ((uint32_t)fileBuffer[19] << 24) | ((uint32_t)fileBuffer[18] << 16) | ((uint32_t)fileBuffer[17] << 8) | ((uint32_t)fileBuffer[16]);
            uint16_t audioFormat = ((uint16_t)fileBuffer[21] << 8) | fileBuffer[20];
            if(subchunk1Size != 16 || audioFormat != 1) {
                std::cout << "Audio format not supported" << std::endl;
                return;
            }

            m_channels = fileBuffer[22];

            m_sampleRate = ((uint32_t)fileBuffer[27] << 24) | ((uint32_t)fileBuffer[26] << 16) | ((uint32_t)fileBuffer[25] << 8) | ((uint32_t)fileBuffer[24]);

            m_bitsPerSample = ((uint32_t)fileBuffer[35] << 8) | ((uint32_t)fileBuffer[34]);

            // Data
            if(fileBuffer[36] != 'd' || fileBuffer[37] != 'a' || fileBuffer[38] != 't' || fileBuffer[39] != 'a') {
                std::cout << "Not data chunk" << std::endl;
                return;
            }

            m_dataSize = ((uint32_t)fileBuffer[43] << 24) | ((uint32_t)fileBuffer[42] << 16) | ((uint32_t)fileBuffer[41] << 8) | ((uint32_t)fileBuffer[40]);

            m_pcmData = new uint8_t[m_dataSize];
            std::memcpy(m_pcmData, fileBuffer + 44, m_dataSize);

            unsigned int bytesPerSample = m_bitsPerSample / 8;
            m_frames = m_dataSize / (bytesPerSample * m_channels);
        }

    private:
        unsigned int m_channels;
        unsigned int m_sampleRate;
        unsigned int m_bitsPerSample;
        unsigned int m_dataSize;
        unsigned int m_frames;
        uint8_t* m_pcmData;
    };

    class AudioDevice {
    public:
        AudioDevice(unsigned int sampleRate, unsigned int periodLength, unsigned int channels) 
                : m_sampleRate(sampleRate), m_periodLength(periodLength), m_channels(channels), m_sampleSize(2) {
            int error;

            error = snd_pcm_open(&m_pcmHandle, "default", SND_PCM_STREAM_PLAYBACK, 0);
            handleAlsaError(error, "could not open sound device");

            snd_pcm_hw_params_t* hwParams;
            error = snd_pcm_hw_params_malloc(&hwParams);
            handleAlsaError(error, "could not allocate hardware parameters");

            error = snd_pcm_hw_params_any(m_pcmHandle, hwParams);
            handleAlsaError(error, "could not get hardware parameters");

            error = snd_pcm_hw_params_set_rate_resample(m_pcmHandle, hwParams, 1);
            handleAlsaError(error, "could set hardware parameter 'rate_resample'");

            error = snd_pcm_hw_params_set_access(m_pcmHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
            handleAlsaError(error, "could not set hardware parameter 'access'");

            error = snd_pcm_hw_params_set_format(m_pcmHandle, hwParams, SND_PCM_FORMAT_S16_LE);
            handleAlsaError(error, "could not set hardware parameter 'format'");

            error = snd_pcm_hw_params_set_rate_near(m_pcmHandle, hwParams, (unsigned int*)&m_sampleRate, 0);
            handleAlsaError(error, "could not set hardware parameter 'rate_near'");

            snd_pcm_uframes_t tPeriodLength = periodLength;
            error = snd_pcm_hw_params_set_period_size_near(m_pcmHandle, hwParams, &tPeriodLength, 0);
            handleAlsaError(error, "could not set hardware parameter 'period_size_near");
            m_periodLength = tPeriodLength;

            error = snd_pcm_hw_params_set_channels(m_pcmHandle, hwParams, m_channels);
            handleAlsaError(error, "could not set hardware parameter 'channels'");

            error = snd_pcm_hw_params(m_pcmHandle, hwParams);
            handleAlsaError(error, "could not set hardware parameters");

            uint bufferSize = m_periodLength * m_channels * 2; // 2 bytes per sample
            m_pcmBuffer = new uint8_t[bufferSize];

            unsigned int periodTime;
            snd_pcm_hw_params_get_period_time(hwParams, &periodTime, 0);

            snd_pcm_hw_params_free (hwParams);
        }

        ~AudioDevice() {
            int error = snd_pcm_drain(m_pcmHandle);
            handleAlsaError(error, "could not drain sound device");

            error = snd_pcm_close(m_pcmHandle);
            handleAlsaError(error, "could not close sound device");

            delete[] m_pcmBuffer;
        }

        void writePcmData() {
            int error = snd_pcm_writei(m_pcmHandle, m_pcmBuffer, m_periodLength);
            if(error == -EPIPE) {
                std::cout << "Underrun occured" << std::endl;
                snd_pcm_prepare(m_pcmHandle);
            }
            else {
                handleAlsaError(error, "Error from snd_pcm_writei");
            }
        }

        unsigned int fillBuffer(unsigned int currentFrame, AudioFile audioFile) {
            if(audioFile.getFrames() < currentFrame) return currentFrame;
            unsigned int bytesPerFrame = (audioFile.getBitsPerSample() / 8) * audioFile.getChannels();
            unsigned int framesLeftInFile = audioFile.getFrames() - currentFrame;
            unsigned int framesToWrite = std::min(getPeriodLength(), framesLeftInFile);

            unsigned int bytesToWrite = framesToWrite * bytesPerFrame;
            std::memcpy(m_pcmBuffer, audioFile.getPcmData() + currentFrame * bytesPerFrame, bytesToWrite);
            
            return currentFrame + framesToWrite;
        }

        double fillBuffer(double time, std::function<double(double)> noiseFunction) {
            int frameSize = 4;
            double t = 0;
            for(int i = 0; i < getPeriodLength(); i++) {
                int16_t v = noiseFunction(t + time) * 4096.0;

                int16_t* buf = (int16_t*)getPcmBuffer();
                unsigned int bufferOffset = i * getChannels();
                buf[bufferOffset] = v;
                buf[bufferOffset + 1] = v;

                t = (double)i / (double)getSampleRate();
            }
            return t + time;
        }

        void playFile(const AudioFile& audioFile) {
            unsigned int frame = 0;
            unsigned int loops = audioFile.getFrames() / getPeriodLength();

            for (int i = 0; i < loops; ++i) {
                frame = fillBuffer(frame, audioFile);
                writePcmData();
            }
        }

        void playFunction(std::function<double(double)> noiseFunction, double time) {
            double currentTime = 0;
            while(currentTime < time) {
                currentTime = fillBuffer(currentTime, noiseFunction);
                writePcmData();
            }
        }

        inline unsigned int getSampleRate() const { return m_sampleRate; }
        inline unsigned int getPeriodLength() const { return m_periodLength; }
        inline unsigned int getChannels() const { return m_channels; }
        inline unsigned int getSampleSize() const { return m_sampleSize; }
        inline uint8_t* getPcmBuffer() const { return m_pcmBuffer; }

    private:
        void handleAlsaError(int error, const char* message) {
            if(error < 0) {
                std::cout << "Audio Library error - " << message << ": " << snd_strerror(error) << std::endl;
                exit(-1);
            }
        }
        
    private:
        unsigned int m_sampleRate; // The number of samples per second
        unsigned int m_periodLength; // The number of frames per period
        unsigned int m_channels; // The number of audio channels
        unsigned int m_sampleSize; // The size(in bytes) of a sample
        snd_pcm_t* m_pcmHandle;
        uint8_t* m_pcmBuffer; // Buffer that stores pcm data for one period
    };

}