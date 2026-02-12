#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <opus/opus.h>
#include <vector>

#define PORT 8080
#define SAMPLE_RATE 48000
#define CHANNELS 2
#define FRAME_SIZE_MS 20
#define FIRST_BITRATE 192000
#define SECOND_BITRATE 128000
#define FOURTH_BITRATE 96000
#define EIGHTH_BITRATE 64000
#define BUFFER_SIZE 13

#define SAMPLES_PER_FRAME (SAMPLE_RATE * FRAME_SIZE_MS / 1000)
#define PCM_BUFFER_SIZE (SAMPLES_PER_FRAME * CHANNELS)

using namespace std;

#pragma pack(push, 1)
struct RTPHeader
{
    int seq;
    long timestamp;
};
#pragma pack(pop)

// We define a frame structure with a large enough buffer for the compressed data
#pragma pack(push, 1)

struct Frame
{
    RTPHeader header;
    unsigned char payload1[(FIRST_BITRATE * FRAME_SIZE_MS / 1000) / 8];
    unsigned char payload2[(SECOND_BITRATE * FRAME_SIZE_MS / 1000) / 8];
    unsigned char payload4[(FOURTH_BITRATE * FRAME_SIZE_MS / 1000) / 8];
    unsigned char payload8[(EIGHTH_BITRATE * FRAME_SIZE_MS / 1000) / 8];
};
#pragma pack(pop)

class Buffer
{
    int curr = -8;

    unsigned char buffer[BUFFER_SIZE][sizeof(Frame::payload1)];
    int sizes[BUFFER_SIZE];

public:
    void fill(const Frame &frame)
    {
        int sequence = frame.header.seq;
        if (sequence > curr && sizes[sequence % BUFFER_SIZE] < sizeof(frame.payload1))
        {
            std::memcpy(buffer[sequence % BUFFER_SIZE], frame.payload1, sizeof(frame.payload1));
            sizes[sequence % BUFFER_SIZE] = sizeof(frame.payload1);
        }
        if (sequence - 1 > curr && sizes[(sequence - 1) % BUFFER_SIZE] < sizeof(frame.payload2))
        {
            std::memcpy(buffer[(sequence - 1) % BUFFER_SIZE], frame.payload2, sizeof(frame.payload2));
            sizes[(sequence - 1) % BUFFER_SIZE] = sizeof(frame.payload2);
        }
        if (sequence - 3 > curr && sizes[(sequence - 3) % BUFFER_SIZE] < sizeof(frame.payload4))
        {
            std::memcpy(buffer[(sequence - 3) % BUFFER_SIZE], frame.payload4, sizeof(frame.payload4));
            sizes[(sequence - 3) % BUFFER_SIZE] = sizeof(frame.payload4);
        }
        if (sequence - 7 > curr && sizes[(sequence - 7) % BUFFER_SIZE] < sizeof(frame.payload8))
        {
            std::memcpy(buffer[(sequence - 7) % BUFFER_SIZE], frame.payload8, sizeof(frame.payload8));
            sizes[(sequence - 7) % BUFFER_SIZE] = sizeof(frame.payload8);
        }
    }

    int read(unsigned char *data)
    {
        int size = sizes[curr % BUFFER_SIZE];
        std::memcpy(data, buffer[curr % BUFFER_SIZE], size);
        sizes[curr % BUFFER_SIZE] = 0;
        curr++;
        return size;
    }
};

int main()
{
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    struct Frame network_buffer;

    // Buffers
    short pcm_out_buffer[PCM_BUFFER_SIZE];

    Buffer buffer;

    // Create a zero-filled buffer for the silence file
    short silence_buffer[PCM_BUFFER_SIZE];
    memset(silence_buffer, 0, sizeof(silence_buffer));

    int err;
    OpusDecoder *decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    if (err < 0)
        return 1;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    // --- OPEN TWO FILES ---
    ofstream file_plc("FOutput.pcm", ios::binary | ios::out);
    // ofstream file_silence("NOutput.pcm", ios::binary | ios::out);

    cout << "Rx Listening..." << endl;
    cout << "1. Output_PLC.pcm (Packet Loss Concealment)" << endl;
    // cout << "2. Output_Silence.pcm (Zero Fill)" << endl;

    // int last_seq = 0;
    // bool first_packet = true;

    unsigned char opus_data[sizeof(Frame::payload1)];
    while (true)
    {
        socklen_t len = sizeof(cliaddr);
        int n = recvfrom(sockfd, &network_buffer, sizeof(Frame), 0, (struct sockaddr *)&cliaddr, &len);

        if (n == sizeof(Frame))
        {
            buffer.fill(network_buffer);

            int len = 0;
            while (len == 0)
            {
                len = buffer.read(opus_data);
                int recovered_samples = 0;
                if (len > 0)
                {
                    recovered_samples = opus_decode(decoder, opus_data, len, pcm_out_buffer, SAMPLES_PER_FRAME, 0);
                }
                else
                {
                    recovered_samples = opus_decode(decoder, NULL, len, pcm_out_buffer, SAMPLES_PER_FRAME, 0);
                }
                if (recovered_samples > 0)
                {
                    file_plc.write((char *)pcm_out_buffer, recovered_samples * CHANNELS * sizeof(short));
                    std::cout << "Decode samples : " << recovered_samples << endl;
                    // file_silence.write((char *)silence_buffer, recovered_samples * CHANNELS * sizeof(short));
                }
                else
                {
                    std::cout << "Errror in decoding " << opus_strerror(recovered_samples) << endl;
                }
            }
        }
    }

    opus_decoder_destroy(decoder);
    close(sockfd);
    file_plc.close();
    // file_silence.close();
    return 0;
}
