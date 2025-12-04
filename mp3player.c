#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mad.h>
#include <alsa/asoundlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

struct buffer {
    unsigned char const *start;
    unsigned long length;
} mp3_file_buffer;

static signed short mad_to_short(mad_fixed_t sample)
{
    if (sample >= MAD_F_ONE)
        return 32767;
    if (sample <= -MAD_F_ONE)
        return -32768;

    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static int decode_output(snd_pcm_t *pcm_handle,
                         struct mad_pcm *pcm)
{
    int nchannels = pcm->channels;
    int nsamples = pcm->length;
    size_t buffer_size = nsamples * nchannels * 2;
    char *buffer = malloc(buffer_size); 
    if (!buffer) {
        perror("malloc failed");
        return -1;
    }
    
    signed short *ptr = (signed short *)buffer;
    mad_fixed_t const *left_ch = pcm->samples[0];
    mad_fixed_t const *right_ch = (nchannels == 2) ? pcm->samples[1] : NULL;
    
    for (int i = 0; i < nsamples; i++) {
        *ptr++ = mad_to_short(left_ch[i]);
        
        if (nchannels == 2) {
            *ptr++ = mad_to_short(right_ch[i]);
        }
    }
    
    snd_pcm_sframes_t frames_written = snd_pcm_writei(pcm_handle, buffer, pcm->length);

    if (frames_written < 0) {
        fprintf(stderr, "ALSA write error: %s\n", snd_strerror(frames_written));
        if (frames_written == -EPIPE) {
            if (snd_pcm_prepare(pcm_handle) < 0) {
                 fprintf(stderr, "ALSA prepare failed.\n");
            }
        }
    }

    free(buffer);
    
    return 0;
}

static enum mad_flow input(void *data,
                          struct mad_stream *stream)
{
    if (!mp3_file_buffer.length)
        return MAD_FLOW_STOP;
    
    mad_stream_buffer(stream, mp3_file_buffer.start, mp3_file_buffer.length);
    mp3_file_buffer.length = 0;
    
    return MAD_FLOW_CONTINUE;
}

static enum mad_flow output(void *data,
                            struct mad_header const *header,
                            struct mad_pcm *pcm)
{
    snd_pcm_t *pcm_handle = (snd_pcm_t *)data; 
    static int alsa_configured = 0; 
    
    if (alsa_configured == 0) {
        int err;
        snd_pcm_hw_params_t *params;
        snd_pcm_hw_params_alloca(&params);
        
        if ((err = snd_pcm_hw_params_any(pcm_handle, params)) < 0) {
            fprintf(stderr, "unable to initialize HW parameter structure (%s)\n", snd_strerror(err));
            return MAD_FLOW_STOP;
        }

        snd_pcm_hw_params_set_access(pcm_handle, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm_handle, params,
                                     SND_PCM_FORMAT_S16_LE);
        
        unsigned int rate = pcm->samplerate;
        snd_pcm_hw_params_set_rate_near(pcm_handle, params,
                                        &rate, 0); 
        
        snd_pcm_hw_params_set_channels(pcm_handle, params,
                                       pcm->channels);
        
        if ((err = snd_pcm_hw_params(pcm_handle, params)) < 0) {
            fprintf(stderr, "Error setting hardware parameters (%s)\n", snd_strerror(err));
            return MAD_FLOW_STOP;
        }

        alsa_configured = 1;
        printf("ALSA ready: Channels=%d, Rate=%uHz\n", pcm->channels, rate);
    }
    
    decode_output(pcm_handle, pcm);

    return MAD_FLOW_CONTINUE;
}

static enum mad_flow error(void *data,
                          struct mad_stream *stream,
                          struct mad_frame *frame)
{
    fprintf(stderr, "Decoding error 0x%04x (%s) at byte %lu\n",
            stream->error, mad_stream_errorstr(stream),
            stream->this_frame - stream->buffer);
    
    if (MAD_RECOVERABLE(stream->error))
        return MAD_FLOW_CONTINUE;

    return MAD_FLOW_STOP;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <mp3_file>\n", argv[0]);
        return 1;
    }
    
    //open the file and get its size too
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return 1;
    }
    long size = st.st_size;
    
    unsigned char *mp3_data = malloc(size);
    if (!mp3_data) {
        perror("malloc");
        close(fd);
        return 1;
    }
    
    if (read(fd, mp3_data, size) != size) {
        perror("read");
        free(mp3_data);
        close(fd);
        return 1;
    }
    close(fd);
    
    mp3_file_buffer.start = mp3_data;
    mp3_file_buffer.length = size;
    
    snd_pcm_t *pcm_handle = NULL;
    int err = snd_pcm_open(&pcm_handle, "default",
                           SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "ALSA error in opening: %s\n", snd_strerror(err));
        free(mp3_data);
        return 1;
    }
    
    struct mad_decoder decoder;
    
    mad_decoder_init(&decoder, pcm_handle,input, 0, 0,output, error, 0); 
    
    printf("Now Playing: %s\n", argv[1]);
    
    mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
    
    mad_decoder_finish(&decoder);
    
    snd_pcm_drain(pcm_handle); 
    snd_pcm_close(pcm_handle);
    free(mp3_data);
    
    printf("Audio Played Successfully.\n");
    
    return 0;
}
