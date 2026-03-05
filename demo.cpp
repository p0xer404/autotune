#include <iostream>
#include <vector>
#include <cmath>
#include <complex>
#include <fftw3.h>
#include <sndfile.h>
#include <string>

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

// ------------------------------
// Windowing Hann
// ------------------------------
void apply_hann_window(std::vector<double>& block) {
    size_t N = block.size();
    for(size_t n = 0; n < N; ++n) {
        block[n] *= 0.5 * (1 - cos(2*M_PI*n/(N-1)));
    }
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
// Processar bloc amb FFT + windowing
// ------------------------------
std::vector<double> process_block(const std::vector<double>& block,
                                  DeviceType device,
                                  double volume,
                                  double bandwidth,
                                  double sr) {
    size_t N = block.size();
    std::vector<double> windowed_block = block;
    apply_hann_window(windowed_block);

    fftw_complex* out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex)*(N/2+1));
    fftw_plan plan_fwd = fftw_plan_dft_r2c_1d(N, windowed_block.data(), out, FFTW_ESTIMATE);
    fftw_execute(plan_fwd);

    std::vector<double> freqs(N/2+1);
    std::vector<double> magnitude(N/2+1);
    for(size_t i = 0; i < N/2+1; ++i) {
        freqs[i] = i * sr / N;
        magnitude[i] = sqrt(out[i][0]*out[i][0] + out[i][1]*out[i][1]);
    }

    std::vector<bool> mask = perceptual_mask(freqs, magnitude, device, volume, bandwidth);

    for(size_t i = 0; i < N/2+1; ++i) {
        if(!mask[i]) { out[i][0] = 0.0; out[i][1] = 0.0; }
    }

    std::vector<double> processed(N);
    fftw_plan plan_inv = fftw_plan_dft_c2r_1d(N, out, processed.data(), FFTW_ESTIMATE);
    fftw_execute(plan_inv);

    fftw_destroy_plan(plan_fwd);
    fftw_destroy_plan(plan_inv);
    fftw_free(out);

    // Normalitzar
    processed = normalize(processed);

    return processed;
}

// ------------------------------
// Processar tot l'àudio amb blocs
// ------------------------------
std::vector<double> process_audio(const std::vector<double>& audio,
                                  DeviceType device,
                                  double volume,
                                  double bandwidth,
                                  double sr) {
    size_t block_size = 4096;
    size_t hop = block_size/2; // 50% solapament
    std::vector<double> output(audio.size(), 0.0);

    for(size_t start=0; start < audio.size(); start += hop) {
        size_t end = std::min(start+block_size, audio.size());
        std::vector<double> block(audio.begin()+start, audio.begin()+end);
        block.resize(block_size, 0.0); // zero-padding

        std::vector<double> processed_block = process_block(block, device, volume, bandwidth, sr);

        for(size_t i=0;i<block_size && (start+i)<audio.size();++i) {
            output[start+i] += processed_block[i] * 0.5; // compensar solapament
        }
    }

    return output;
}

// ------------------------------
// WAV I/O
// ------------------------------
std::vector<double> read_wav(const std::string& filename, int& sr) {
    SF_INFO sfinfo;
    SNDFILE* sndfile = sf_open(filename.c_str(), SFM_READ, &sfinfo);
    if(!sndfile) { std::cerr<<"Error obrint fitxer WAV\n"; exit(1); }
    sr = sfinfo.samplerate;
    std::vector<double> buffer(sfinfo.frames*sfinfo.channels);
    sf_readf_double(sndfile, buffer.data(), sfinfo.frames);
    sf_close(sndfile);
    return buffer;
}

void write_wav(const std::string& filename, const std::vector<double>& audio, int sr) {
    SF_INFO sfinfo;
    sfinfo.samplerate = sr;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* sndfile = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
    if(!sndfile) { std::cerr<<"Error guardant fitxer WAV\n"; exit(1); }
    sf_writef_double(sndfile, audio.data(), audio.size());
    sf_close(sndfile);
}

// ------------------------------
// MAIN
// ------------------------------
int main() {
    std::cout<<"=== Demo Perceptual Masking ===\n";
    std::cout<<"Tria dispositiu (0=DESKTOP,1=MOBILE,2=EARBUDS,3=SMART_SPEAKER): ";
    int device_choice; std::cin>>device_choice;
    DeviceType device = static_cast<DeviceType>(device_choice);

    std::cout<<"Introdueix volum (0.0-1.0): ";
    double volume; std::cin>>volume;

    std::cout<<"Introdueix bandwidth (0.0-1.0): ";
    double bandwidth; std::cin>>bandwidth;

    std::string input_file = "input.wav";
    std::string output_file = "output_demo.wav";
    int sr;
    auto audio = read_wav(input_file, sr);

    std::cout<<"Processant àudio...\n";
    auto processed_audio = process_audio(audio, device, volume, bandwidth, sr);

    write_wav(output_file, processed_audio, sr);
    std::cout<<"Processament complet! Fitxer guardat: "<<output_file<<"\n";

    return 0;
}
