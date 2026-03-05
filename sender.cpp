#include <iostream>
#include <vector>
#include <cmath>
#include <complex>
#include <fftw3.h>
#include <sndfile.h>
#include <string>
#include <thread>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#define CLOSESOCKET closesocket
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#define CLOSESOCKET close
#endif

#ifdef _WIN32
#define DEVICE_NAME "Windows Desktop"
#elif __APPLE__
#define DEVICE_NAME "Mac Desktop"
#else
#define DEVICE_NAME "Linux Desktop"
#endif

enum DeviceType { DESKTOP, MOBILE, EARBUDS, SMART_SPEAKER };

// ------------------------------
// Funcions auxiliars
// ------------------------------
std::vector<double> normalize(const std::vector<double>& audio) {
    std::vector<double> out(audio.size());
    double max_val = 0.0;
    for(double v : audio) if(fabs(v) > max_val) max_val = fabs(v);
    for(size_t i = 0; i < audio.size(); ++i)
        out[i] = (max_val > 0) ? audio[i]/max_val : audio[i];
    return out;
}

double rmse(const std::vector<double>& original, const std::vector<double>& processed) {
    double sum = 0.0;
    for(size_t i = 0; i < original.size(); ++i)
        sum += (original[i] - processed[i]) * (original[i] - processed[i]);
    return sqrt(sum / original.size());
}

// ------------------------------
// Perceptual mask
// ------------------------------
std::vector<bool> perceptual_mask(const std::vector<double>& freqs,
                                  const std::vector<double>& magnitude,
                                  DeviceType device,
                                  double volume,
                                  double bandwidth) {
    std::vector<bool> mask(freqs.size(), true);
    double low_cutoff, high_cutoff;

    switch(device) {
        case MOBILE:
        case EARBUDS: low_cutoff = 80; high_cutoff = 12000; break;
        case SMART_SPEAKER: low_cutoff = 60; high_cutoff = 16000; break;
        case DESKTOP:
        default: low_cutoff = 30; high_cutoff = 20000; break;
    }

    for(size_t i = 0; i < freqs.size(); ++i) {
        if(freqs[i] < low_cutoff || freqs[i] > high_cutoff) mask[i] = false;
        if(volume < 0.5 && freqs[i] > 10000) mask[i] = false;
        if(bandwidth < 0.5 && (freqs[i] < 300 || freqs[i] > 3400)) mask[i] = false;
    }

    return mask;
}

// ------------------------------
// FFT + iFFT per canal
// ------------------------------
std::vector<double> process_channel(const std::vector<double>& channel_data,
                                    DeviceType device,
                                    double volume,
                                    double bandwidth,
                                    double sr) {
    size_t N = channel_data.size();
    fftw_complex* out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (N/2 + 1));
    fftw_plan plan_fwd = fftw_plan_dft_r2c_1d(N, const_cast<double*>(channel_data.data()), out, FFTW_ESTIMATE);
    fftw_execute(plan_fwd);

    std::vector<double> freqs(N/2 + 1);
    std::vector<double> magnitude(N/2 + 1);
    for(size_t i = 0; i < N/2 + 1; ++i) {
        freqs[i] = i * sr / N;
        magnitude[i] = sqrt(out[i][0]*out[i][0] + out[i][1]*out[i][1]);
    }

    std::vector<bool> mask = perceptual_mask(freqs, magnitude, device, volume, bandwidth);

    for(size_t i = 0; i < N/2 + 1; ++i) {
        if(!mask[i]) { out[i][0] = 0.0; out[i][1] = 0.0; }
    }

    std::vector<double> processed(N);
    fftw_plan plan_inv = fftw_plan_dft_c2r_1d(N, out, processed.data(), FFTW_ESTIMATE);
    fftw_execute(plan_inv);

    processed = normalize(processed);

    fftw_destroy_plan(plan_fwd);
    fftw_destroy_plan(plan_inv);
    fftw_free(out);

    return processed;
}

// ------------------------------
// Stereo processing
// ------------------------------
std::vector<std::vector<double>> process_stereo(const std::vector<std::vector<double>>& stereo_frame,
                                                DeviceType device,
                                                double volume,
                                                double bandwidth,
                                                double sr) {
    size_t n_channels = stereo_frame.size();
    std::vector<std::vector<double>> processed_audio(n_channels);
    for(size_t ch = 0; ch < n_channels; ++ch)
        processed_audio[ch] = process_channel(stereo_frame[ch], device, volume, bandwidth, sr);
    return processed_audio;
}

// ------------------------------
// WAV I/O
// ------------------------------
std::vector<std::vector<double>> read_wav(const std::string& filename, int& sr) {
    SF_INFO sfinfo;
    SNDFILE* sndfile = sf_open(filename.c_str(), SFM_READ, &sfinfo);
    if(!sndfile) { std::cerr << "Error obrint fitxer WAV\n"; exit(1); }

    sr = sfinfo.samplerate;
    int channels = sfinfo.channels;
    size_t frames = sfinfo.frames;
    std::vector<double> buffer(frames * channels);
    sf_readf_double(sndfile, buffer.data(), frames);
    sf_close(sndfile);

    std::vector<std::vector<double>> stereo_frame(channels, std::vector<double>(frames));
    for(int c = 0; c < channels; ++c)
        for(size_t i = 0; i < frames; ++i)
            stereo_frame[c][i] = buffer[i*channels + c];

    return stereo_frame;
}

void write_wav(const std::string& filename, const std::vector<std::vector<double>>& stereo_frame, int sr) {
    SF_INFO sfinfo;
    sfinfo.samplerate = sr;
    sfinfo.channels = stereo_frame.size();
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    size_t frames = stereo_frame[0].size();
    std::vector<double> buffer(frames * sfinfo.channels);

    for(int c = 0; c < sfinfo.channels; ++c)
        for(size_t i = 0; i < frames; ++i)
            buffer[i*sfinfo.channels + c] = stereo_frame[c][i];

    SNDFILE* sndfile = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
    if(!sndfile) { std::cerr << "Error guardant fitxer WAV\n"; exit(1); }
    sf_writef_double(sndfile, buffer.data(), frames);
    sf_close(sndfile);
}

// ------------------------------
// TCP Server per rebre dispositiu
// ------------------------------
struct DeviceInfo {
    DeviceType device;
    double volume;
    double bandwidth;
};

DeviceInfo parse_json(const std::string& msg) {
    auto j = json::parse(msg);
    std::string type = j["device_type"];
    DeviceType dev = DESKTOP;
    if(type=="mobile") dev = MOBILE;
    else if(type=="earbuds") dev = EARBUDS;
    else if(type=="smart_speaker") dev = SMART_SPEAKER;
    return { dev, j["volume"], j["bandwidth"] };
}

DeviceInfo wait_for_client(int port=5000) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){ std::cerr<<"Error socket\n"; exit(1); }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(server_fd, (struct sockaddr*)&addr, sizeof(addr))<0){
        std::cerr<<"Error bind\n"; exit(1);
    }
    listen(server_fd, 1);
    std::cout<<"Esperant receptor a port "<<port<<"...\n";

    int client_fd = accept(server_fd, nullptr, nullptr);
    if(client_fd<0){ std::cerr<<"Error accept\n"; exit(1); }

    char buffer[1024];
    int len = recv(client_fd, buffer, sizeof(buffer)-1, 0);
    buffer[len] = '\0';

    CLOSESOCKET(client_fd);
    CLOSESOCKET(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return parse_json(buffer);
}

// ------------------------------
// MAIN
// ------------------------------
int main() {
    std::string input_file = "input.wav";
    std::string output_file = "output_processed.wav";
    int sr;
    auto stereo_audio = read_wav(input_file, sr);

    DeviceInfo info = wait_for_client();
    std::cout << "Receptor connectat! Processant àudio segons:\n";
    std::cout << "Device type: "<< info.device << " Volume: "<< info.volume << " Bandwidth: "<< info.bandwidth << "\n";

    auto processed_audio = process_stereo(stereo_audio, info.device, info.volume, info.bandwidth, sr);

    std::cout << "Processament complet. Guardant fitxer...\n";
    write_wav(output_file, processed_audio, sr);

    std::cout << "Fitxer guardat a: " << output_file << "\n";
    return 0;
}
