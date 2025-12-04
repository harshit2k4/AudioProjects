#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <FLAC/stream_decoder.h>
#include <alsa/asoundlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

// available playback states
typedef enum {
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_STOPPED,
    STATE_QUIT
} player_state_t;

volatile player_state_t current_state = STATE_PLAYING;

typedef struct {
    snd_pcm_t *pcm_handle;
    unsigned char *file_data;
    long file_size;
    long current_offset;
    int alsa_configured;
} playback_state_t;

static int configure_alsa(snd_pcm_t *pcm_handle, unsigned int rate, unsigned int channels)
{
    int err;
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    
    if ((err = snd_pcm_hw_params_any(pcm_handle, params)) < 0) {
        fprintf(stderr, "ALSA: Failed to init HW parameters (%s)\n", snd_strerror(err));
        return -1;
    }

    snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE);
    
    unsigned int actual_rate = rate;
    snd_pcm_hw_params_set_rate_near(pcm_handle, params, &actual_rate, 0); 
    
    snd_pcm_hw_params_set_channels(pcm_handle, params, channels);
    
    snd_pcm_uframes_t period_size = 512; 
    snd_pcm_uframes_t buffer_size = 2048; 

    snd_pcm_hw_params_set_period_size_near(pcm_handle, params, &period_size, 0);
    snd_pcm_hw_params_set_buffer_size_near(pcm_handle, params, &buffer_size);
    
    if ((err = snd_pcm_hw_params(pcm_handle, params)) < 0) {
        fprintf(stderr, "ALSA: Error setting hardware parameters (%s)\n", snd_strerror(err));
        return -1;
    }
    printf("  [Audio] Ready! Channels: %u | Rate: %u Hz | Buffer: %lu frames\n", 
           channels, actual_rate, buffer_size);
           
    return 0;
}

static FLAC__StreamDecoderReadStatus flac_read_callback(
    const FLAC__StreamDecoder *decoder,
    FLAC__byte buffer[],
    size_t *bytes,
    void *client_data)
{
    if (current_state == STATE_STOPPED || current_state == STATE_QUIT) {
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
    
    playback_state_t *state = (playback_state_t *)client_data;

    if (state->current_offset >= state->file_size) {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }

    long bytes_to_read = (long)*bytes;
    long remaining = state->file_size - state->current_offset;
    
    if (bytes_to_read > remaining) {
        bytes_to_read = remaining;
    }

    if (bytes_to_read == 0) {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    memcpy(buffer, state->file_data + state->current_offset, bytes_to_read);
    state->current_offset += bytes_to_read;
    *bytes = bytes_to_read;
    
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus flac_write_callback(
    const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[],
    void *client_data)
{
    playback_state_t *state = (playback_state_t *)client_data;
    snd_pcm_t *pcm_handle = state->pcm_handle;
    if (current_state == STATE_STOPPED || current_state == STATE_QUIT) {
        snd_pcm_drop(pcm_handle);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    if (current_state == STATE_PAUSED) {
        snd_pcm_pause(pcm_handle, 1);
        
        while (current_state == STATE_PAUSED) {
            usleep(100000); 
            if (current_state == STATE_STOPPED || current_state == STATE_QUIT) {
                snd_pcm_drop(pcm_handle);
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }
        }
    }
    
    snd_pcm_pause(pcm_handle, 0); 
    size_t nchannels = frame->header.channels;
    size_t nsamples = frame->header.blocksize;
    size_t buffer_size = nsamples * nchannels * 2; 
    
    signed short *output_buffer = malloc(buffer_size);
    if (!output_buffer) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    
    signed short *ptr = output_buffer;
    
    for (size_t i = 0; i < nsamples; i++) {
        *ptr++ = (signed short)buffer[0][i]; 
        if (nchannels == 2) {
            *ptr++ = (signed short)buffer[1][i]; 
        }
    }

    snd_pcm_sframes_t frames_written = snd_pcm_writei(pcm_handle, output_buffer, nsamples);

    if (frames_written < 0) {
        if (frames_written == -EPIPE) {
            fprintf(stderr, "  [ALSA Error] Underrun detected, preparing...\n");
            snd_pcm_prepare(pcm_handle);
        } else {
            fprintf(stderr, "  [ALSA Error] Write error: %s\n", snd_strerror(frames_written));
        }
    }
    
    free(output_buffer);
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_metadata_callback(
    const FLAC__StreamDecoder *decoder,
    const FLAC__StreamMetadata *metadata,
    void *client_data)
{
    playback_state_t *state = (playback_state_t *)client_data;

    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        if (!state->alsa_configured) {
            
            unsigned int rate = metadata->data.stream_info.sample_rate;
            unsigned int channels = metadata->data.stream_info.channels;
            
            printf("  [FLAC] Channels: %u | Sample Rate: %u Hz\n", channels, rate);
            
            if (configure_alsa(state->pcm_handle, rate, channels) == 0) {
                state->alsa_configured = 1;
            }
        }
    }
}

static void flac_error_callback(
    const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus status,
    void *client_data)
{
    fprintf(stderr, "  [FLAC Error] Decoding error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

void *input_monitor(void *arg) {
    char command;
    system("stty cbreak -echo"); 
    
    printf("--------------------------------------------------\n");
    printf("  CONTROLS: (p)ause, (r)esume, (s)top, (q)uit\n");
    printf("--------------------------------------------------\n");

    while (current_state != STATE_QUIT && current_state != STATE_STOPPED) {
        command = getchar();
        
        switch (command) {
            case 'p': 
                if (current_state == STATE_PLAYING) {
                    current_state = STATE_PAUSED;
                    printf(">> PAUSED. (Press 'r' to resume)\n");
                }
                break;
            case 'r': 
                if (current_state == STATE_PAUSED) {
                    current_state = STATE_PLAYING;
                    printf(">> RESUMED.\n");
                }
                break;
            case 's': 
                current_state = STATE_STOPPED; 
                printf(">> STOPPED. Cleaning up...\n");
                break;
            case 'q': 
                current_state = STATE_QUIT;
                printf(">> QUIT SIGNALED. Exiting...\n");
                break;
        }
    }
    
    system("stty cooked echo"); 
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <flac_file>\n", argv[0]);
        return 1;
    }
    
    printf("\n--- FLAC Player CLI ---\n");
    printf("  [File] Opening: %s\n", argv[1]);
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("  [Error] open");
        return 1;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("  [Error] fstat");
        close(fd);
        return 1;
    }
    long size = st.st_size;
    printf("  [File] Size: %ld bytes\n", size);

    unsigned char *file_data = malloc(size);
    if (!file_data) {
        perror("  [Error] malloc");
        close(fd);
        return 1;
    }
    
    if (read(fd, file_data, size) != size) {
        perror("  [Error] read");
        free(file_data);
        close(fd);
        return 1;
    }
    close(fd);

    // setup ALSA as per requirement
    snd_pcm_t *pcm_handle = NULL;
    int err = snd_pcm_open(&pcm_handle, "default",
                           SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "  [ALSA Error] Failed to open PCM device: %s\n", snd_strerror(err));
        free(file_data);
        return 1;
    }
    playback_state_t state = {
        .pcm_handle = pcm_handle,
        .file_data = file_data,
        .file_size = size,
        .current_offset = 0,
        .alsa_configured = 0
    };
    
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (decoder == NULL) {
        fprintf(stderr, "  [Error] Failed to create FLAC decoder.\n");
        goto cleanup;
    }

    FLAC__stream_decoder_init_stream(
        decoder,
        flac_read_callback, NULL, NULL, NULL, NULL,
        flac_write_callback,
        flac_metadata_callback,
        flac_error_callback,
        &state
    );
    
    pthread_t input_thread;
    if (pthread_create(&input_thread, NULL, input_monitor, NULL) != 0) {
        perror("  [Error] pthread_create failed");
        FLAC__stream_decoder_delete(decoder);
        goto cleanup;
    }

    FLAC__stream_decoder_process_until_end_of_stream(decoder);
    pthread_join(input_thread, NULL); 
    FLAC__stream_decoder_delete(decoder);

    cleanup:
    snd_pcm_drain(pcm_handle); 
    snd_pcm_close(pcm_handle);
    free(file_data);
    
    printf("\n--- Playback Session Ended ---\n");
    
    return 0;
}
