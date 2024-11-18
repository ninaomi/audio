#define _CRT_SECURE_NO_WARNINGS

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

#include <SDL.h>
#include <SDL_thread.h>
}
#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <mmreg.h>
#pragma comment(lib, "winmm.lib")

#pragma comment(lib, "comctl32.lib")

AVFrame*** buffer = 0;


WAVEFORMATEX waveFormat;
int  finished_decoding=0;
    
    
int fill_buffer()
{
    // Open video file
    AVFormatContext* pFormatCtx = avformat_alloc_context();
    if (!pFormatCtx)
    {
        fprintf(stderr, "Could not allocate memory for format context");
        return -1;
    }

    if (avformat_open_input(&pFormatCtx, "1.mp4", NULL, NULL) != 0)
    {
        fprintf(stderr, "Cannot open file");
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        fprintf(stderr, "Could not find stream information");
        return -1; // Couldn't find stream information
    }

    const AVCodec* pCodec = NULL;
    AVCodecParameters* pCodecParams = NULL;

    // Find the first video stream
    int videoStream = -1;
    int i;

    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
            pCodecParams = pFormatCtx->streams[i]->codecpar;
            pCodec = avcodec_find_decoder(pCodecParams->codec_id);
            break; // We want only the first video stream, the leave the other stream that might be present in the file
        }
    }

    int audioStream = -1;
    int k;

    const AVCodec* paCodec = NULL;
    AVCodecParameters* paCodecParams = NULL;

    for (k = 0; k < pFormatCtx->nb_streams; k++)
    {
        if (pFormatCtx->streams[k]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStream = k;
            paCodecParams = pFormatCtx->streams[k]->codecpar;
            paCodec = avcodec_find_decoder(paCodecParams->codec_id);
            break; // We want only the first video stream, the leave the other stream that might be present in the file
        }
    }

    if (audioStream == -1) {
        fprintf(stderr, "Could not find audio stream\n");
        return -1;
    }

    // Get a pointer to the codec context for the video stream
    AVCodecContext* pCodecCtx = avcodec_alloc_context3(pCodec);
    if (pCodecCtx == NULL)
    {
        fprintf(stderr, "Could not allocate video codec context");
        return -1;
    }
    AVCodecContext* paCodecCtx = avcodec_alloc_context3(paCodec);//for the audio
    if (paCodecCtx == NULL)
    {
        fprintf(stderr, "Could not allocate video codec context");
        return -1;
    }

    // Fill the codec context from the codec parameters values
    if (avcodec_parameters_to_context(pCodecCtx, pCodecParams) < 0)//video
    {
        fprintf(stderr, "Failed to copy codec params to codec context");
        return -1;
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)//video
    {
        fprintf(stderr, "Failed to open codec");
        return -1;
    }

    if (avcodec_parameters_to_context(paCodecCtx, paCodecParams) < 0)//audio
    {
        fprintf(stderr, "Failed to copy codec params to codec context");
        return -1;
    }

    if (avcodec_open2(paCodecCtx, paCodec, NULL) < 0)//audio
    {
        fprintf(stderr, "Failed to open codec");
        return -1;
    }

    // Allocate video frame
    AVFrame* pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        fprintf(stderr, "Could not allocate video frame");
        return -1;
    }

    AVPacket* pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for AVPacket");
        return -1;
    }

    AVPacket* audiopacket = av_packet_alloc();
    AVFrame* audioframe = av_frame_alloc();

    AVFrame* pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL)
    {
        fprintf(stderr, "Could not allocate output video frame");
        return -1;
    }
    /////////////////

    SwrContext* swrCtx = nullptr;
    swr_alloc_set_opts2(&swrCtx, &paCodecCtx->ch_layout, AV_SAMPLE_FMT_FLT, paCodecCtx->sample_rate, &paCodecCtx->ch_layout, paCodecCtx->sample_fmt, paCodecCtx->sample_rate, 0, nullptr);
    swr_init(swrCtx);

    


    // Initialize the wave format based on decoded audio properties
    waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    waveFormat.nChannels = paCodecCtx->ch_layout.nb_channels;
    waveFormat.nSamplesPerSec = paCodecCtx->sample_rate;
    waveFormat.wBitsPerSample = av_get_bytes_per_sample(paCodecCtx->sample_fmt) * 8;//paCodecCtx->sample_fmt; // Assuming AV_SAMPLE_FMT_S16
    waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    waveFormat.cbSize = 0;

   


    /////////////////////


    AVFrame* newfr = av_frame_alloc();
    int index = 0;

    //AVAudioFifo* fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, paCodecCtx->ch_layout.nb_channels,1);

    while (av_read_frame(pFormatCtx, pPacket) >= 0)
    {
       
        // Is this a packet from the video stream?
        if (pPacket->stream_index == audioStream)
        {
            // Decode video frame  
            if (avcodec_send_packet(paCodecCtx, pPacket) < 0)
            {
                fprintf(stderr, "Error while sending packet to decoder.");
                return -1;
            }

            int frameFinished = 1;
            // Did we get a video frame?
            while (frameFinished >= 0)
            {
                frameFinished = avcodec_receive_frame(paCodecCtx, audioframe);
                // These two return values are special and mean there is no output
                // frame available, but there were no errors during decoding
                if (frameFinished == 0)
                {
                    

                    newfr->sample_rate = audioframe->sample_rate;
                    newfr->ch_layout = audioframe->ch_layout;
                    //newfr->ch_layout.nb_channels = audioframe->ch_layout.nb_channels;
                    newfr->format = AV_SAMPLE_FMT_FLT;


                    swr_convert_frame(swrCtx, newfr, audioframe);
                    if (index < 4000)
                    {
                        // b:
                        if (*buffer[0][index]->buf == 0)
                        {
                            *buffer[0][index]->buf = *newfr->buf;
                            *(buffer[0][index]->data) = *(newfr->data);
                            *buffer[0][index]->linesize = *newfr->linesize;
                            buffer[0][index]->sample_rate = newfr->sample_rate;
                            buffer[0][index]->nb_samples = newfr->nb_samples;
                        }
                        // else goto b;
                        index++;
                        printf("%d\n", index);
                    }
                    else break;
                    /*else {
                        index = 0;
                    
                    p:
                        if (*buffer[0][index]->buf == 0)
                            buffer[0][index] = newfr;
                        else goto p;
                        index++;
                    
                    }*/


                    
                    av_frame_unref(newfr);

                }
                if (frameFinished == AVERROR(EAGAIN) || frameFinished == AVERROR_EOF)
                {
                    break;
                }
                else if (frameFinished < 0)
                {
                    fprintf(stderr, "Error during decoding");
                    return -1;
                }

                // Convert the image into YUV format that SDL uses
                if (frameFinished >= 0)
                {




                    

                }
            }

            // Free the packet that was allocated by av_read_frame
            av_packet_unref(pPacket);
            

        }
    }
    finished_decoding = 1;
    // Free the YUV frame
    av_frame_free(&pFrame);
    av_frame_free(&newfr);
    av_frame_free(&pFrameRGB);

    // Close the codec
    avcodec_free_context(&pCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);


    return 0;
}


int play_buffer()
{
    HWAVEOUT hWaveOut = 0;
    WAVEHDR waveHeaders = { 0 };


    int sleeptime;

    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &waveFormat, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to open audio output.\n");
        return -1;
    }
    int i = 0;

    while(i<4000)
    {
        if(i<3200)
        { 
           // l:
            if (*buffer[0][i]->buf != 0)
            {
                waveHeaders.dwBufferLength = buffer[0][i]->linesize[0];
                waveHeaders.lpData = (char*)buffer[0][i]->data[0];
                waveOutPrepareHeader(hWaveOut, &waveHeaders, sizeof(WAVEHDR));
                waveOutWrite(hWaveOut, &waveHeaders, sizeof(WAVEHDR));

                if (buffer[0][i]->sample_rate)
                 {
                     sleeptime = (buffer[0][i]->nb_samples * 1000) / buffer[0][i]->sample_rate;
                     Sleep(sleeptime);
                 }
                waveOutUnprepareHeader(hWaveOut, &waveHeaders, sizeof(WAVEHDR));


                 
                 //else Sleep();

                av_frame_unref(buffer[0][i]);

                printf("current frame %d\n",i);
                i++;
            }
            //else break;//goto l;

        }
        else break;
       /* else
        {
            i = 0;
            Q:
            if (*buffer[0][i]->buf != 0)
            {
                waveHeaders.dwBufferLength = buffer[0][i]->linesize[0];
                waveHeaders.lpData = (char*)buffer[0][i]->data[0];
                waveOutPrepareHeader(hWaveOut, &waveHeaders, sizeof(WAVEHDR));
                waveOutWrite(hWaveOut, &waveHeaders, sizeof(WAVEHDR));

                waveOutUnprepareHeader(hWaveOut, &waveHeaders, sizeof(WAVEHDR));



                if (buffer[0][i]->sample_rate)
                {
                    sleeptime = (buffer[0][i]->nb_samples * 1000) / buffer[0][i]->sample_rate;
                    Sleep(sleeptime);
                }

                av_frame_unref(buffer[0][i]);
                i++;
            }
            else goto Q;

        }
*/
       
    }


    waveOutClose(hWaveOut);
    return 0;
}





LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE:
    {
        // Get new width and height of the window
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        // Move/resize controls based on new window dimensions
        
        MoveWindow(GetDlgItem(hwnd,10), 10, height - 100, width - 20, 40, TRUE);  // Example of resizing a trackbar

        // Add more controls and their respective repositioning logic here

        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int main(int argc, char* argv[])
{
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    buffer = (AVFrame***)malloc( sizeof(AVFrame**));
    memset(buffer, 0, sizeof(AVFrame**));

    buffer[0] = (AVFrame**)malloc(4000 * sizeof(AVFrame*));
    memset(buffer[0], 0, sizeof(AVFrame*));

    for (int i = 0;i <4000;i++)
    {
      
        buffer[0][i] = (AVFrame*)malloc(sizeof(AVFrame));
        memset(buffer[0][i], 0, sizeof(AVFrame));
    } 
    
    WNDCLASS wc = {};

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"videoclass";

    if (!RegisterClass(&wc))
        return -1;

    HWND hwnd = CreateWindowEx(0, L"videoclass", L"Video Player", WS_OVERLAPPEDWINDOW, (screenWidth - 800) / 2, (screenHeight - 600) / 2, 800, 600, NULL, NULL, wc.hInstance, NULL);
    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    
    
    CreateWindowEx(0, TRACKBAR_CLASS, NULL, WS_CHILD | WS_VISIBLE | TBS_HORZ, 50, 50, 300, 30, hwnd, (HMENU)10, 0, NULL);

    HANDLE thread1,thread2;  // Declare a thread handle

    // Create a new thread
    thread1 = CreateThread(NULL, 0, (PTHREAD_START_ROUTINE)fill_buffer, NULL, 0, NULL);
    if (thread1 == NULL) {
        printf("Failed to create thread.\n");
        return 1;
    }

    WaitForSingleObject(thread1, INFINITE);

    thread2 = CreateThread(NULL, 0, (PTHREAD_START_ROUTINE)play_buffer, NULL, 0, NULL);
    if (thread2 == NULL) {
        printf("Failed to create thread.\n");
        return 1;
    }
    
    WaitForSingleObject(thread1, INFINITE);
    CloseHandle(thread1);


    WaitForSingleObject(thread2, INFINITE);
    CloseHandle(thread2); 
    
    MSG msg;
   
    

    while (PeekMessage(&msg, NULL, 0, 0,0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}