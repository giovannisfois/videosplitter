
/* Output file format configuration */

#define OUTPUT_FORMAT     ".wmv" 
#define VIDEO_OUT_CODEC_ID    AV_CODEC_ID_WMV2 
#define VIDEO_OUT_WIDTH   320
#define VIDEO_OUT_HEIGHT  240
#define VIDEO_OUT_BITRATE 7500000
#define VIDEO_OUT_PIX_FMT AV_PIX_FMT_YUV420P
#define VIDEO_OUT_FRAMERATE_NUM 25
#define VIDEO_OUT_FRAMERATE_DEN 1

#define VIDEO_IN_FIFO_SIZE    5000
#define AUDIO_VIDEO_MAX_DELAY 1

#define AUDIO_OUT_CODEC_ID    AV_CODEC_ID_WMAV2
#define AUDIO_OUT_SAMPLE_FMT  AV_SAMPLE_FMT_FLTP
#define AUDIO_OUT_CHANNELS    2

/*Audio Sample Rate - Bit Rate and samples per frame are strongly related
  You can't choose them as freely as you wish - TODO: explain and adapt these numbers
  The values choosen are meant to avoid extra buffering during the audio conversion
*/
#define AUDIO_OUT_BITRATE     96000
#define AUDIO_OUT_SAMPLE_RATE 48000
#define AUDIO_OUT_SAMPLES_PER_FRAME 1152

//This works, but I don't know why.
#define MAGIC_NUMBER_ALPHA 2

#define DEBUG 0

#define INTERLEAVED 1

/* During the conversion of videos there a ton of things that you need to keep track of.
   We keep the details in two global structs which will hold all the meaingfull data for the
   input and output streams.
*/

/* The output struct holder prototype */

typedef struct output_struct{

  // The conteiner for the audio video output streams
  AVFormatContext *av_format_context;

  // A flag, tell if we have an output file open
  // Will be set when opening and closing the output file 
  int         isVideoOutOpen;

  // The output filename
  char    *filename;

  // The prefix for the output files
  char    *prefix;

  //The output files counter, will be used with the prefix to form the filename es.: prefix_10.wmv
  int      fileCounter;


  //The output packet, will be used when saving packets to the file
  AVPacket   packet;

  /******* Video ******************/

  // The output video stream
  AVStream   *video_stream ;

  // The output CODEC time base which is frame based (VIDEO_OUT_FRAMERATE_NUM/VIDEO_OUT_FRAMERATE_DEN) 
  // This is not the video_stream timebase, wich has a value like 1000
  AVRational  video_time_base;

  // Output Video Frame (converted and ready to be saved)
  AVFrame    *video_frame;

  // Rescaling context for the video rescaling
  struct SwsContext *video_sws_ctx;

  // We need to count the skipped video frames, in order to sync audio and video
  int         video_frames_skipped;

  /******* Audio ******************/

  // The Output Audio Stream
  AVStream   *audio_stream ;


  // The output CODEC time base (1/AUDIO_OUT_BITRATE), which is sample based
  // Audio and video differs in this
  // This is not the video_stream timebase, wich has a value like 1000
  AVRational  audio_time_base;
  AVCodecContext *audio_write_ctx;


  // Output Audio Frame (will be filled with the resampled samples)
  AVFrame     *audio_frame;

  // Output Audio Frame (Codec) whill hold the samples sent to the codec 
  AVFrame     *audio_codec_frame;

  // How many samples have we processed until now?
  // Note Samples, not frames
  int64_t      audio_samples_count;

  // The audio samples fifo
  /* Processing video frames is easy: one frame enters, one frame exits
     Processing audio frames is a messy thing
     A frame enters, his samples are extracted, and stored away.
     When you have enough samples, you will repack them in a new frame and
     send them to the codec.
     The Codec, tipically, wants a specific number of samples, which is not the
     one you get with every audio frame. This is true even in you are converting
     from and to the SAME format/bitrate/etc.
     So you will need an AVAudioFifo wich  will hold the samples queue
  */
  AVAudioFifo *audio_samples_fifo ;

  // Resampling context for the audio resampling.
  // Resampling audio between formats is SLOW
  // When converting you will tipycally try to keep the same AUDIO sampling rate.
  SwrContext *audio_resample_ctx;


  int64_t      video_pts;
  int64_t      audio_pts;


} output_data_struct;


/* The input struct holder prototype */
typedef struct input_struct{

  // The container for the input streamsa
  AVFormatContext *av_format_context;
  
  // The input filename
  char    *filename;

  //The input packet, will be used when saving packets to the file, both for video and audio streams
  AVPacket   packet;

  /***** VIDEO *******/

  // The id for the video stream
  int video_stream_id;

  // The last read video frame
  AVFrame    *video_frame;
  AVFrame    *video_frame_fifo[VIDEO_IN_FIFO_SIZE];

  int         video_frame_fifo_head;
  int         video_frame_fifo_tail;

  // A flag. When reading from the input stream we read audio or video frames
  // This flag tell us if there is a video frame waiting to be processed
  int        has_video_frame;

  /***** AUDIO *******/
  int audio_stream_id;

  // The last read audio frame
  AVFrame    *audio_frame;

  // A flag. When reading from the input stream we read audio or video frames
  // This flag tell us if there is an audio frame waiting to be processed
  int        has_audio_frame;


} input_data_struct;


/*Procedures declarations*/

/* Init the input data structure with the default values */
void init_input_data_struct(input_data_struct *input_struct);

/* Init the output data structure with the default values */
void init_output_data_struct(output_data_struct *output_struct);

/* Search the input stream for a media type (audio or video) and open the matching codec */
int  open_input_codec_context_id( AVFormatContext *pFormatCtx, enum AVMediaType type );

/* Open the output file and set the output video stream */
int openOutputFileStream(output_data_struct *output_struct);

/* Open the output audio codec */
int openOutputAudioCodec(output_data_struct *output_struct);

/* Open the video audio codec */
int openOutputVideoCodec(output_data_struct *output_struct);

/* Initialize the video scaling context, data from the input and output struct is needed */
int initVideoScaling(output_data_struct *output_struct, input_data_struct *input_struct);

/* Initialize the video resampling context, data from the input and output struct is needed */
int initAudioResampling(output_data_struct *output_struct, input_data_struct *input_struct);

/* Save a video frame in the output file */
int saveVideoFrameOut(output_data_struct *output_struct, input_data_struct *input_struct);

/* Save one or more audio frames in the output file */
int saveAudioFrameOut(output_data_struct *output_struct, input_data_struct *input_struct);

/* Close the output video and his output streams */
void closeOutputFileStream(output_data_struct *output_struct);

/* Is this frame a black frame? */
int isBlackFrame(AVFrame *pFrame);

/* Tell how many pixels from a frame's rectangle are black */
int countBlackPixels(AVFrame *pFrame, int rectW, int rectH, int threshold);


void YUV2RGB(int Y, int Cr, int Cb, char *colors);
static const char *get_error_text(const int error);

/* DEBUG functions */

#if DEBUG == 1
void my_log_callback(void *ptr, int level, const char *fmt, va_list vargs) { printf("----> ");printf (fmt,vargs);}
#endif

