#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <opus/opus.h>
#include <chrono>

/* --- Opus & Audio Configuration --- */
#define PORT 8080
#define SAMPLE_RATE 48000
#define CHANNELS 2
#define FRAME_SIZE_MS 20
#define FIRST_BITRATE 192000
#define SECOND_BITRATE 128000
#define FOURTH_BITRATE 96000
#define EIGHTH_BITRATE 64000 
#define BUFFER_SIZE 13
#define PACKET_LOSS_PERCENTAGE 0 // Simulate 5% packet loss for testing

// 48000 * 0.02 = 960 samples per channel
#define SAMPLES_PER_FRAME (SAMPLE_RATE * FRAME_SIZE_MS / 1000)
// 960 samples * 2 channels * 2 bytes (short) = 3840 bytes per raw PCM frame
#define PCM_FRAME_BYTES (SAMPLES_PER_FRAME * CHANNELS * sizeof(short))

using namespace std;

#pragma pack(push, 1)
struct RTPHeader {
    int seq;
    long timestamp;
};
#pragma pack(pop)


// We define a frame structure with a large enough buffer for the compressed data
#pragma pack(push, 1)

struct Frame {
    RTPHeader header;
    unsigned char payload1[(FIRST_BITRATE * FRAME_SIZE_MS /1000)/8];
    unsigned char payload2[(SECOND_BITRATE * FRAME_SIZE_MS /1000)/8];
    unsigned char payload4[(FOURTH_BITRATE * FRAME_SIZE_MS /1000)/8];
    unsigned char payload8[(EIGHTH_BITRATE * FRAME_SIZE_MS /1000)/8];
};
#pragma pack(pop)

class Buffer{
    short buffer[BUFFER_SIZE][SAMPLES_PER_FRAME * CHANNELS];

    public:

    void fill(int seq, short* pcmdata){
        std::memcpy(buffer[(seq + BUFFER_SIZE)%BUFFER_SIZE], pcmdata, SAMPLES_PER_FRAME * CHANNELS * sizeof(short)); 
    }

    short* getData(int seq){
        return buffer[(seq + BUFFER_SIZE)%BUFFER_SIZE];
    }
};

void encoder_init (OpusEncoder*& first, OpusEncoder*& second, OpusEncoder*& fourth, OpusEncoder*& eighth, int& err){
    first = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    if(err < 0) return;
    second = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    if(err < 0) return;
    fourth = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    if(err < 0) return;
    eighth = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    if(err < 0) return;

    opus_encoder_ctl(first, OPUS_SET_BITRATE(FIRST_BITRATE));
    opus_encoder_ctl(second, OPUS_SET_BITRATE(SECOND_BITRATE));
    opus_encoder_ctl(fourth, OPUS_SET_BITRATE(FOURTH_BITRATE));
    opus_encoder_ctl(eighth, OPUS_SET_BITRATE(EIGHTH_BITRATE));
    
    opus_encoder_ctl(first, OPUS_SET_VBR(0));
    opus_encoder_ctl(second, OPUS_SET_VBR(0));
    opus_encoder_ctl(fourth, OPUS_SET_VBR(0));
    opus_encoder_ctl(eighth, OPUS_SET_VBR(0));

    opus_encoder_ctl(first, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(second, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(fourth, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(eighth, OPUS_SET_COMPLEXITY(10));

    opus_encoder_ctl(first, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(second, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(fourth, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(eighth, OPUS_SET_INBAND_FEC(0));
}

int main() {
    // 1. Open the raw PCM file (Must be 48kHz, Stereo, S16LE)
    ifstream file("Input.pcm", ios::binary);
    if (!file.is_open()) {
        cerr << "Error opening Input.pcm! Ensure it is raw S16LE 48kHz Stereo." << endl;
        return 1;
    }

    // 2. Initialize Opus Encoder
    int err = 0;
    OpusEncoder* enc1 = nullptr;
    OpusEncoder* enc2 = nullptr;
    OpusEncoder* enc4 = nullptr;
    OpusEncoder* enc8 = nullptr;
   
    encoder_init(enc1, enc2, enc4, enc8, err);
    if(err < 0) {
        std::cout<< "Error in creating encoder"<<endl;
        return 1;
    }

    // 3. Setup UDP Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 4. Processing Buffers
    cout << "Transmitting Opus stream to 127.0.0.1:" << PORT << "..." << endl;
    
    short data[SAMPLES_PER_FRAME * CHANNELS];
    Buffer buffer;
    int sequence = 0;
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    long timestamp = now_ms.time_since_epoch().count();


    // 5. Encoding and Transmission Loop
    // We must read exactly PCM_FRAME_BYTES (3840 bytes) for a 20ms frame

    srand(timestamp);
    while (file.read((char*)data, PCM_FRAME_BYTES)) {
        
        Frame frame;
        frame.header.seq = sequence;
        frame.header.timestamp = timestamp;

        //
        buffer.fill(sequence, data);

        // Encode PCM to Opus at all four bitrates
        int p1 = opus_encode(enc1, (buffer.getData(sequence)), SAMPLES_PER_FRAME, frame.payload1, sizeof(frame.payload1));
        int p2 = opus_encode(enc2, (buffer.getData(sequence-1)), SAMPLES_PER_FRAME, frame.payload2, sizeof(frame.payload2));
        int p4 = opus_encode(enc4, (buffer.getData(sequence-3)), SAMPLES_PER_FRAME, frame.payload4, sizeof(frame.payload4));
        int p8 = opus_encode(enc8, (buffer.getData(sequence-8)), SAMPLES_PER_FRAME, frame.payload8, sizeof(frame.payload8));



        if (p1 < 0 || p2 < 0 || p4 < 0 || p8 < 0) {
            cerr << "Encoding failed: "  << endl;
            break;
        }

        // Packet loss simulation (uncomment to enable)
        int prob = rand() % 100;
        if (prob < PACKET_LOSS_PERCENTAGE) {
            cout << "Simulated packet loss for Seq: " << sequence << endl;
        }else{


            
            // Send the header + all four payloads
            int total_send_size = sizeof(RTPHeader) + p1 + p2 + p4 + p8;
            int sent = sendto(sockfd, &frame, sizeof(frame), 0,
                (const struct sockaddr *)&servaddr, sizeof(servaddr));
                
                if (sent < 0) {
                    perror("Error sending packet");
                } else {
                    // Log the compression performance
                    cout << "Sent Seq: " << (sequence) << " | Compressed: " << PCM_FRAME_BYTES << " -> " << sizeof(frame) << "=" <<total_send_size << " bytes" << endl;
            }
        }

        // Real-time pacing: 20ms frame means we wait 20ms
        // Adjust for processing time if needed, but 20ms is the standard pace
        sequence++;
        timestamp += 20;
        usleep(FRAME_SIZE_MS * 100); 
    }

    cout << "Transmission complete." << endl;

    // Cleanup
    if (enc1) opus_encoder_destroy(enc1);
    if (enc2) opus_encoder_destroy(enc2);
    if (enc4) opus_encoder_destroy(enc4);
    if (enc8) opus_encoder_destroy(enc8);
    close(sockfd);
    file.close();
    return 0;
}
