/* This code will convert a video to the wmv format
   During the conversion each commercial detected will be extracted and saved in a separated file
*/

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h> 
#include <libswresample/swresample.h> 

//#include <libavutil/channel_layout.h>
//#include <libavutil/samplefmt.h>
//#include <libavutil/parseutils.h>

#include <stdio.h>

#include "videosplitter.h"

/* Let's start */
int main(int argc, char *argv[]){

  //will hold the asprintfresults
  int asprintfresult;

  int ret;

  output_data_struct output_struct;
  input_data_struct  input_struct;

#if DEBUG == 1
  //For debugging purposes
  setbuf(stdout, NULL);
#endif

  //Init input struct with the default values
  init_input_data_struct(&input_struct);

  //Init output struct with the default values
  init_output_data_struct(&output_struct);

  //Print Usage if the argument's count is wrong
  if (argc < 3) {
    fprintf(stderr, "\nusage: %s inputFile outputPrefix\n",
	   argv[0]);
    exit(0);
  }

  //Read input file name
  asprintfresult = asprintf(&input_struct.filename, "%s", argv[1]);
  if(asprintfresult == -1){
    fprintf(stderr, "\nError, can't read the filename\n");
    return -1;
  }

  //Read output file prefix
  asprintfresult = asprintf(&output_struct.prefix, "%s", argv[2]);
  if(asprintfresult == -1){
    fprintf(stderr, "\nErrore, non riesco a leggere il nome del file\n");
    return -1;
  }
  
  //Compose the name for the output file (prefix + counter + extension)
  asprintfresult = asprintf(&output_struct.filename,"%s_%d%s",
			    output_struct.prefix,
			    output_struct.fileCounter,
			    OUTPUT_FORMAT);

  fprintf(stderr, "\nConverting file: %s -> %s\n",input_struct.filename, output_struct.filename);

  //Init the avlib (or ffmpeg) libraries
  av_register_all();

  //Open the input file
  if ((ret = avformat_open_input(&input_struct.av_format_context, input_struct.filename, NULL, NULL)) < 0) {
    fprintf(stderr, "\nCould not open input file '%s'\n", input_struct.filename);
    exit(0);
  }

  //Read packets of a media file to get the stream information. 
  if ((ret = avformat_find_stream_info(input_struct.av_format_context, 0)) < 0) {
    fprintf(stderr, "\nFailed to retrieve input stream information\n");
    exit(0);
  }

  /* Print detailed information about the input or output format, such as duration, bitrate, streams, container, programs, metadata, side data, codec and time base. */
  av_dump_format(input_struct.av_format_context, 0, input_struct.filename, 0);

  //Open the video codec for the input video stream - and get the stream_id, in the process
  input_struct.video_stream_id  = open_input_codec_context_id(input_struct.av_format_context, AVMEDIA_TYPE_VIDEO);

  //Open the audio codec for the input audio stream - and get the stream_id, in the process
  input_struct.audio_stream_id  = open_input_codec_context_id(input_struct.av_format_context, AVMEDIA_TYPE_AUDIO);

  if(input_struct.video_stream_id == -1){
    fprintf(stderr, "\nFailed to open input video decoder\n");
    goto end;
  }

  if(input_struct.audio_stream_id == -1){
    fprintf(stderr, "\nFailed to open input audio decoder\n");
    goto end;
  }

  //Let's print some info
  fprintf(stderr, "\nVideo stream id: %d\n",input_struct.video_stream_id);
  fprintf(stderr, "\nAudio stream id: %d\n",input_struct.audio_stream_id);

  fprintf(stderr, "\nWidth: %d - Height: %d - Frame Format: %d\n",
	 input_struct.av_format_context->streams[input_struct.video_stream_id]->codec->width,
	 input_struct.av_format_context->streams[input_struct.video_stream_id]->codec->height,
	 input_struct.av_format_context->streams[input_struct.video_stream_id]->codec->pix_fmt);


  if(!input_struct.av_format_context->streams[input_struct.video_stream_id]->codec->width ||
     !input_struct.av_format_context->streams[input_struct.video_stream_id]->codec->height){
    fprintf(stderr, "\nFailed to get Width and Height from the file.\n");
    goto end;
  }


#if DEBUG == 1
  av_log_set_level(AV_LOG_DEBUG);
  av_log_set_callback(my_log_callback);
#endif

  //Create the output format context for writing the output streams and open the output file
  if(!openOutputFileStream(&output_struct)){
    goto end;
  }


  //Init video rescaling (each video frame will be rescaled)
  if(!initVideoScaling(&output_struct,&input_struct)){
    goto end;
  }



  //Init audio resampling (each audio frame fill be resampled)
  //Audio resampling is a time consuming activity.
  //We will try to keep the original samples rate/
  //Nevertheles: Audio samples are going to be extracted from each frame and afterwards are repacked in a new frame
  if(!initAudioResampling(&output_struct,&input_struct)){
    goto end;
  }



  //Flag: tell if we need to open a new output file
  int canOpenNewOutputFile = 0;

  //Print some stuff about the progress
  AVStream * inputVideoStream = input_struct.av_format_context->streams[input_struct.video_stream_id];
  int64_t totalDuration	  = (inputVideoStream->duration-inputVideoStream->start_time) * inputVideoStream->time_base.num / inputVideoStream->time_base.den;
  int64_t outputDuration = 0;
  int isFirstVideoFrame  = 1;
  int blackFramesFlag  = 0;

  fprintf(stderr, "\nDuration   : %"PRIu64" \n",inputVideoStream->duration   * inputVideoStream->time_base.num / inputVideoStream->time_base.den );
  fprintf(stderr, "\nStart Time : %"PRIu64" \n",inputVideoStream->start_time * inputVideoStream->time_base.num / inputVideoStream->time_base.den );

  while(readAFrame(&input_struct, &output_struct)){
    /* We have just read a frame fromt the input file.
       We can have:  a video frame to save in the output video stream
                    a bunch of audio samples, maybe we have enough for pack an audio frame
    */

    if(input_struct.has_audio_frame){
      //Let see if the audio input fifo has enough samples for building a frame
      if(!saveAudioFrameOut(&output_struct,&input_struct)){
	goto end;
      }
    }
    

    //If we have a video frame waiting in input_struct->video_frame
    if(input_struct.has_video_frame){

      //Print some stuff about the progress
      outputDuration = output_struct.video_stream->nb_frames * VIDEO_OUT_FRAMERATE_DEN / VIDEO_OUT_FRAMERATE_NUM;
      fprintf(stderr, "Converted : %02"PRIu64":%02"PRIu64"  of  %02"PRIu64":%02"PRIu64"  \r", outputDuration/60, outputDuration%60 , totalDuration/60 ,totalDuration%60 );
      
      //Look, a malformed video_frame, let's ignore it
      if(input_struct.video_frame->pkt_pts == 0){
	fprintf(stderr, "\nSkip a BAD video frame!\n");
	input_struct.has_video_frame = 0;
	continue;
      }

      //Rescale the frame and save it in the output video stream
      if(!saveVideoFrameOut(&output_struct,&input_struct)){
	goto end;
      }

      if(isBlackFrame(output_struct.video_frame)){
	blackFramesFlag = 1;
      }else{
	if(blackFramesFlag){
	  blackFramesFlag = 0;
	  //canOpenNewOutputFile = 1;
	}
      }
    }

/*
    printf("\nAudio Time Base: %d/%d \n", output_struct.audio_stream->time_base.num , output_struct.audio_stream->time_base.den);
    printf("\nVideo Time Base: %d/%d \n", output_struct.video_stream->time_base.num , output_struct.video_stream->time_base.den);

    printf("\nAudio Frames: %"PRIu64" \n", output_struct.audio_stream->nb_frames);
    printf("\nVideo Frames: %"PRIu64" \n", output_struct.video_stream->nb_frames);
*/

    //countBlackFrames++;
    //We will open a new output file only when needed (not after each black frame)
    if(canOpenNewOutputFile){
      
      //The output counter will restart.
      totalDuration -= outputDuration;
      
      //Reset the flag
      canOpenNewOutputFile = 0;
      
      //Close the current output file and open a new file
      free(output_struct.filename);
      output_struct.fileCounter++;
      asprintfresult = asprintf(&output_struct.filename,"%s_%d%s",
				output_struct.prefix,
				output_struct.fileCounter,
				OUTPUT_FORMAT);
      
      //Close the current output video file
      closeOutputFileStream(&output_struct);
      
	//Open a new output video file
      if(!openOutputFileStream(&output_struct)){
	goto end;
      }
    }
  }      
  
  
  //We are done
  closeOutputFileStream(&output_struct);

  fprintf(stderr, "\n\n\nDONE!\n\n");
  
 end:
  //Cleanup of the various stuff
  avformat_close_input(&input_struct.av_format_context);
  free(input_struct.filename);
  free(output_struct.prefix);
  av_free(output_struct.filename);

  
  return 0;
}

//Init output structure - just assignations
void init_output_data_struct(output_data_struct *output_struct){

  output_struct->filename     = NULL;
  output_struct->prefix       = NULL;
  output_struct->fileCounter  = 0;

  output_struct->isVideoOutOpen      = 0;

  output_struct->av_format_context   = NULL;

  output_struct->video_stream        = NULL;
  output_struct->video_time_base.num = VIDEO_OUT_FRAMERATE_DEN ;
  output_struct->video_time_base.den = VIDEO_OUT_FRAMERATE_NUM ;

  output_struct->video_sws_ctx         = NULL;

  output_struct->audio_stream          = NULL;
  output_struct->audio_resample_ctx    = NULL;
  output_struct->audio_time_base.num   = 1 ;
  output_struct->audio_time_base.den   = AUDIO_OUT_SAMPLE_RATE ;

  output_struct->audio_samples_count     = 0 ;
  output_struct->audio_samples_fifo      = NULL ;

  output_struct->video_frame       = NULL ;
  output_struct->audio_frame       = NULL ;
  output_struct->audio_codec_frame = NULL ;
  output_struct->video_frames_skipped = 0 ;

  //Init the output packet, one will do for both video and audio
  av_init_packet(&(output_struct->packet));
  output_struct->packet.data = NULL;
  output_struct->packet.size = 0;
}

//init the input data structure - just assignations
void init_input_data_struct(input_data_struct *input_struct){

  input_struct->filename            = NULL;
  input_struct->av_format_context   = NULL;

  input_struct->video_stream_id  = -1;
  input_struct->audio_stream_id  = -1;

  input_struct->has_video_frame         = 0 ;
  input_struct->has_audio_frame         = 0 ;

  //Alloc the audio and the video input frames 
  //We need both frames because they will have different caracteristics
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1)
  input_struct->video_frame     = av_frame_alloc();
  input_struct->audio_frame     = av_frame_alloc();
#else
  input_struct->video_frame     = avcodec_alloc_frame();
  input_struct->audio_frame     = avcodec_alloc_frame();
#endif

  //Init the input video queue
  input_struct->video_frame_fifo_head = -1;
  input_struct->video_frame_fifo_tail = -1;
  int index;
  for(index=0; index< VIDEO_IN_FIFO_SIZE; index++){
    input_struct->video_frame_fifo[index] = NULL;
  }


  //Init the input packet, one will do for both video and audio
  av_init_packet(&(input_struct->packet));
  input_struct->packet.data = NULL;
  input_struct->packet.size = 0;

}


/* open_input_codec_context_id 
   Look for the first stream of the stated "type", then open the corresponding codec and return the matching stream_id
*/

int open_input_codec_context_id( AVFormatContext *pInputFormatCtx, enum AVMediaType type ){
  int stream_id = -1;

  // Now pFormatCtx->streams is just an array of pointers, 
  // of size pFormatCtx->nb_streams, so let's walk through it until we find a video stream.

  int i;
  AVStream *input_st;
  AVCodecContext *input_decoder_ctx = NULL;
  AVCodec *input_decoder = NULL;

  stream_id = -1;

  // Find the first matching stream
  for(i=0; i<pInputFormatCtx->nb_streams; i++){
    if(pInputFormatCtx->streams[i]->codec->codec_type == type) {
      stream_id=i;
      break;
    }
  }
  
  //We have the stream_id, now we need to open the corresponding codec
  if(stream_id >= 0){

    input_st          = pInputFormatCtx->streams[stream_id];                //The input stream
    input_decoder_ctx = input_st->codec;                                    //His codec context - holds various infos
    input_decoder     = avcodec_find_decoder(input_decoder_ctx->codec_id);  //His codec - we need to open this

    printf( "Open %s codec number: %d\n", av_get_media_type_string(type), input_decoder_ctx->codec_id);

    if (!input_decoder) {
      printf( "Failed to find %s codec\n", av_get_media_type_string(type));
      return -1;
    }

    // We open the codec input_decoder, 
    // wich will be accessed through the input_stream's -> codec 
    // Which is itself a codec_context.
    // Be warned! It's easy to be confused by codec and codec_context, often they are exchanged
    if (avcodec_open2(input_st->codec, input_decoder, NULL) < 0) {
      fprintf(stderr, "\nFailed to open %s codec\n",  av_get_media_type_string(type));
      return -1;
    }else{
      fprintf(stderr, "\nOpened %s codec\n", av_get_media_type_string(type));
    }
  }else{
    printf( "Failed to open %s codec \n", av_get_media_type_string(type));
    return -1;
  }

  return stream_id;
}

/* openOutputFileStream open the output stream and resets the necessary output structure field
   returns 1 in case of success
*/
int openOutputFileStream(output_data_struct *output_struct){

  //We need to do this every time we open a new output file
  output_struct->audio_samples_count     = 0 ;

  /* allocate the output media context for the new output stream */
  output_struct->av_format_context = avformat_alloc_context();
  if (!output_struct->av_format_context) {
    fprintf(stderr, "\nMemory error\n");
    return 0;
  }

  //try to guess the output file format from the name
  output_struct->av_format_context->oformat = av_guess_format(NULL,OUTPUT_FORMAT,NULL);
  if(output_struct->av_format_context->oformat == NULL){
    fprintf(stderr, "\nCan't find a suitable output format\n");
    return 0;
  }

  //av_format_context->filename has been preallocated avformat_alloc_context
  snprintf(output_struct->av_format_context->filename, 
	   sizeof(output_struct->av_format_context->filename),
	   "%s", 
	   output_struct->filename);

  //Open the output video codec - and the matching codec context
  if(!openOutputVideoCodec(output_struct)){
    return 0;
  }

  //Open the output audiocodec - and the matching codec context
  if(!openOutputAudioCodec(output_struct)){
    return 0;
  }

  /* open the output file, if needed */
  if (!(output_struct->av_format_context->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_struct->av_format_context->pb, output_struct->filename, AVIO_FLAG_WRITE) < 0) {
      fprintf(stderr, "\nCould not open '%s'\n", output_struct->filename);
      return 0;
    }
  }

  /* write the stream header, if any */
  avformat_write_header(output_struct->av_format_context ,NULL);

  /* set the flag */
  output_struct->isVideoOutOpen = 1;

  fprintf(stderr, "\nOpened output file: '%s' .'\n", output_struct->filename);
  
  /* everything is ok */
  return 1;
}


void closeOutputFileStream(output_data_struct *output_struct){

  AVFormatContext *outputFormatContext = output_struct->av_format_context;
  int i ;

  /* write the trailer, if any.  the trailer must be written
   * before you close the CodecContexts open when you wrote the
   * header; otherwise write_trailer may try to use memory that
   * was freed on av_codec_close() */

  av_write_trailer(outputFormatContext);

  /* close each codec */
  if (output_struct->video_stream){
    avcodec_close(output_struct->video_stream->codec);
  }

  if (output_struct->audio_stream){
    avcodec_close(output_struct->audio_stream->codec);
  }
  

  /* free the streams */
  for(i = 0; i < outputFormatContext->nb_streams; i++) {
    av_freep(&outputFormatContext->streams[i]->codec);
    av_freep(&outputFormatContext->streams[i]);
  }

  if (!(output_struct->av_format_context->oformat->flags & AVFMT_NOFILE)) {
    /* close the output file */
    avio_flush(outputFormatContext->pb);
    avio_close(outputFormatContext->pb);
  }


  /* free the stream */
  av_free(outputFormatContext);

  output_struct->video_frames_skipped = 0 ;
  output_struct->audio_samples_count = 0;


  output_struct->isVideoOutOpen = 0;  



  fprintf(stderr, "\n\nClosed video file %s\n", output_struct->filename);

}


/* opens the output video codec (and context)
   video codec and audio codec are different, so they need different actions
   VIDEO_OUT_CODEC_ID and the other video params are constant
   defined in videosplitter.h
*/
int openOutputVideoCodec(output_data_struct *output_struct){
  
  AVStream       *video_stream = NULL;
  AVCodecContext *video_codec_ctx = NULL;
  AVCodec        *video_codec = NULL;
  AVOutputFormat *video_format;
  int             error;
  
  /* find the video codec for encoding - we are saving wmv2 files*/
  video_codec = avcodec_find_encoder(VIDEO_OUT_CODEC_ID);
  
  if (!video_codec) {
    fprintf(stderr, "\nvideo codec not found\n");
    return 0;
  }

  //create a new video stream
  video_stream =  avformat_new_stream(output_struct->av_format_context, video_codec);
  if (!video_stream) {
    fprintf(stderr, "\nCould not alloc stream\n");
    return 0;
  }


  //Populate the video stream video codec_context with our options
  video_codec_ctx             = video_stream->codec;
  video_codec_ctx->time_base  = output_struct->video_time_base; //for video frames is 1/framerate



  video_codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  video_codec_ctx->codec_id   = VIDEO_OUT_CODEC_ID ;
  video_codec_ctx->bit_rate   = VIDEO_OUT_BITRATE;
  video_codec_ctx->width      = VIDEO_OUT_WIDTH;
  video_codec_ctx->height     = VIDEO_OUT_HEIGHT;
  video_codec_ctx->pix_fmt    = VIDEO_OUT_PIX_FMT;

  /* emit one intra frame every twelve frames at most */
  video_codec_ctx->gop_size = 12;

  //some formats - like wmv - require a global header
  if (output_struct->av_format_context->oformat->flags & AVFMT_GLOBALHEADER){
    video_codec_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }
  
  /* open the codec */
  if (avcodec_open2(video_codec_ctx, video_codec, NULL) < 0) {
    fprintf(stderr, "\nvideo codec could not open codec\n");
    fprintf(stderr, "\nError String: %d %s \n", error, get_error_text(error));
    return 0;
  }

  /*We keep a direct pointer to the video stream, but you can still access
    it through the output_struct->av_format_context->streams structure */
  output_struct->video_stream = video_stream;

  fprintf(stderr, "\nThe video codec is open\n");

  return 1;
}


/* opens the output audio codec (and context)
   audio codec and video codec are different, so they need different actions
   AUDIO_OUT_CODEC_ID and the other video params are constant
   defined in videosplitter.h
*/
int openOutputAudioCodec(output_data_struct *output_struct){

  AVStream       *audio_stream = NULL;
  AVCodecContext *audio_codec_ctx = NULL;
  AVCodec        *audio_codec = NULL;
  AVCodec        *audio_write = NULL;
  AVOutputFormat *audio_format;
  int             error;
  int             block_align;


  /* find the audio encoder */
  audio_codec = avcodec_find_encoder(AUDIO_OUT_CODEC_ID);

  if (!audio_codec) {
    fprintf(stderr, "\naudio codec not found\n");
    return 0;
  }

  //Create the audio stream
  audio_stream =  avformat_new_stream(output_struct->av_format_context, audio_codec);
  if (!audio_stream) {
    fprintf(stderr, "\nCould not alloc audio stream\n");
    return 0;
  }

  audio_stream->time_base = output_struct->audio_time_base ; //for audio frames is 1/samplerate

  //Populate the Audio codec context
  audio_codec_ctx              = audio_stream->codec;
  audio_codec_ctx->codec_type  = AVMEDIA_TYPE_AUDIO;
  audio_codec_ctx->codec_id    = AUDIO_OUT_CODEC_ID;
  audio_codec_ctx->sample_rate = AUDIO_OUT_SAMPLE_RATE;
  audio_codec_ctx->bit_rate    = AUDIO_OUT_BITRATE;
  audio_codec_ctx->channels    = AUDIO_OUT_CHANNELS; //Number of channels
  audio_codec_ctx->channel_layout = av_get_default_channel_layout(AUDIO_OUT_CHANNELS); //stereo
  audio_codec_ctx->sample_fmt  = AUDIO_OUT_SAMPLE_FMT; //samples format

  if (output_struct->av_format_context->oformat->flags & AVFMT_GLOBALHEADER){
    //required by wmv format
    audio_codec_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }

  /* open the codec */
  if ((error=avcodec_open2(audio_codec_ctx, audio_codec, NULL)) < 0) {
    fprintf(stderr, "\ncould not open audio codec\n");
    fprintf(stderr, "\nError String: %d %s \n", error, get_error_text(error));
    return 0;
  }

  /*We keep a direct pointer to the audio stream, but you can still access
    it through the output_struct->av_format_context->streams structure */
  output_struct->audio_stream = audio_stream;

  return 1;
}

/* Each frame will be scaled, we need to init an SwsContext, which will scale each frame.
   It need to know the output format (constants set by us) and the input format (from the input codec context)
   This procedure:
   - init the output video frame
   - init the scaling context
*/
int initVideoScaling(output_data_struct *output_struct, input_data_struct *input_struct){
  AVFrame *pFrameOut         = NULL;
  struct SwsContext *sws_ctx = NULL;  
  uint8_t *buffer            = NULL;
  int numBytes;
  
  AVCodecContext *input_video_codec_ctx = NULL;

  int input_video_stream_id   = input_struct->video_stream_id;
  int ret;

  
  //The input Video codec context
  input_video_codec_ctx = input_struct->av_format_context->streams[input_video_stream_id]->codec;

  //An empty frame this frame will be our output frame, will hold each scaled frame.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1)
  pFrameOut = av_frame_alloc();
#else
  pFrameOut = avcodec_alloc_frame();
#endif

  //Set the output video frame options
  pFrameOut->format = VIDEO_OUT_PIX_FMT;
  pFrameOut->width  = VIDEO_OUT_WIDTH    ;
  pFrameOut->height = VIDEO_OUT_HEIGHT   ;

  // The frame need a data buffer. 
  // We need to allocate a buffer with the right size.
  // Determine required buffer size and allocate buffer
  numBytes=avpicture_get_size(VIDEO_OUT_PIX_FMT,
			      VIDEO_OUT_WIDTH,
			      VIDEO_OUT_HEIGHT);

  numBytes += FF_INPUT_BUFFER_PADDING_SIZE;

  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

  // Assign appropriate parts of buffer to image planes in pFrame
  // Note that pFrame is an AVFrame, but AVFrame is a superset
  // of AVPicture
  ret = avpicture_fill((AVPicture *)pFrameOut, 
		       buffer, 
		       VIDEO_OUT_PIX_FMT,
		       VIDEO_OUT_WIDTH,
		       VIDEO_OUT_HEIGHT);

  if(ret<0){
    return 0;
  }

  //Save the new frame for later
  output_struct->video_frame     = pFrameOut;

  
  
  //initialize SWS context for software scaling
  sws_ctx = sws_getContext(input_video_codec_ctx->width,
			   input_video_codec_ctx->height,
			   input_video_codec_ctx->pix_fmt,
			   VIDEO_OUT_WIDTH,
			   VIDEO_OUT_HEIGHT,
			   VIDEO_OUT_PIX_FMT,
			   SWS_BILINEAR,
			   NULL,
			   NULL,
			   NULL
			   );

  if(sws_ctx == NULL){
    return 0;
  }

  //Save the scaling context for the later processing
  output_struct->video_sws_ctx   = sws_ctx;
  
  return 1;
}

/* 
   Creates an empty frame for output, a resampling context and a fifo for the audio samples 
*/
int initAudioResampling(output_data_struct *output_struct, input_data_struct *input_struct){
  SwrContext *resample_ctx = NULL;  
  AVCodecContext *input_codec_context  = NULL;
  AVCodecContext *output_codec_context = NULL;
  int error;
  int input_audio_stream_id   = input_struct->audio_stream_id;

  AVFrame *pFrameOut         = NULL;
  struct SwsContext *sws_ctx = NULL;  
  uint8_t *buffer            = NULL;
  int numBytes;
  int ret;
  
  //An empty frame - this frame will be our output frame, it will hold the audio samples
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1)
  pFrameOut = av_frame_alloc();
#else
  pFrameOut = avcodec_alloc_frame();
#endif
  
  pFrameOut->format         = AUDIO_OUT_SAMPLE_FMT;
  pFrameOut->sample_rate    = AUDIO_OUT_SAMPLE_RATE;
  pFrameOut->channel_layout = av_get_default_channel_layout(AUDIO_OUT_CHANNELS);//stereo
  pFrameOut->nb_samples     = AUDIO_OUT_SAMPLES_PER_FRAME;

  //We need to init the data part of the audio frame
  numBytes  = AUDIO_OUT_SAMPLES_PER_FRAME * av_get_bytes_per_sample(AUDIO_OUT_SAMPLE_FMT)*AUDIO_OUT_CHANNELS;
  numBytes += FF_INPUT_BUFFER_PADDING_SIZE;

  buffer   = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

  ret = avcodec_fill_audio_frame(pFrameOut,
				 AUDIO_OUT_CHANNELS,
				 AUDIO_OUT_SAMPLE_FMT,
				 buffer,
				 numBytes,
				 1);

  if(ret<0){
    return 0;
  }


  //Save the audio frame for the later processing
  output_struct->audio_frame          = pFrameOut;

  /*The output audio frame has been created, now we need to prepare another frame for the codec output */



  //An empty frame - this frame will be our output frame, it will hold the audio samples
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1)
  pFrameOut = av_frame_alloc();
#else
  pFrameOut = avcodec_alloc_frame();
#endif

 
  pFrameOut->format         = AUDIO_OUT_SAMPLE_FMT;
  pFrameOut->sample_rate    = AUDIO_OUT_SAMPLE_RATE;
  pFrameOut->channel_layout = av_get_default_channel_layout(AUDIO_OUT_CHANNELS);//stereo

  //This is the difference, the output codec need frames with a different number of samples
  pFrameOut->nb_samples     = output_struct->audio_stream->codec->frame_size ;

  numBytes = output_struct->audio_stream->codec->frame_size * av_get_bytes_per_sample(AUDIO_OUT_SAMPLE_FMT)*AUDIO_OUT_CHANNELS;
  numBytes += FF_INPUT_BUFFER_PADDING_SIZE;

  buffer   = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

  avcodec_fill_audio_frame(pFrameOut,
			   AUDIO_OUT_CHANNELS,
			   AUDIO_OUT_SAMPLE_FMT,
			   buffer,
			   numBytes,
			   1);

    
  
  output_struct->audio_codec_frame          = pFrameOut;  

  /*The output audio codec frame has been created, now we need to prepare the resampling context*/

  //Will tell us the input stream info
  input_codec_context = input_struct->av_format_context->streams[input_audio_stream_id]->codec;

  //An emptu resample context
  resample_ctx = swr_alloc();
  if (!resample_ctx) {
    fprintf(stderr, "\nCould not allocate resample context\n");
    return 0;
  }

  //It's the equivalent of a php hash, a dictionary wich sets the resampling options

  //input
  av_opt_set_int(resample_ctx, "in_channel_layout",  av_get_default_channel_layout(input_codec_context->channels),0);
  av_opt_set_int(resample_ctx, "in_sample_rate",     input_codec_context->sample_rate,                0);
  av_opt_set_int(resample_ctx, "in_sample_fmt",      input_codec_context->sample_fmt, 0);
  av_opt_set_sample_fmt(resample_ctx, "in_sample_fmt",      input_codec_context->sample_fmt, 0);

  //output
  av_opt_set_int(resample_ctx, "out_channel_layout", av_get_default_channel_layout(AUDIO_OUT_CHANNELS),  0);
  av_opt_set_int(resample_ctx, "out_sample_rate",    AUDIO_OUT_SAMPLE_RATE,                0);
  av_opt_set_int(resample_ctx, "out_sample_fmt",     AUDIO_OUT_SAMPLE_FMT, 0);
  av_opt_set_sample_fmt(resample_ctx, "out_sample_fmt",     AUDIO_OUT_SAMPLE_FMT, 0);


  /** Open the resampler with the specified parameters. */
  if ((error = swr_init(resample_ctx)) < 0) {
    fprintf(stderr, "\nCould not open resample context\n");
    swr_free(&resample_ctx);
    return 0;
  }

  //The resampling context is ready.
  output_struct->audio_resample_ctx   = resample_ctx;

  //Now, converting and resampling is not a simple task.
  //Video conversion is: one frame in => one frame out
  //Audio conversion is: one frame in => extract the samples => another frame in => extract the samples
  //We have enough samples for the codec? Yes? Then feed enough frames to the codec
  //So we need to keep the samples in a fifo queue
  output_struct->audio_samples_fifo   = av_audio_fifo_alloc(AUDIO_OUT_SAMPLE_FMT, 
							    AUDIO_OUT_CHANNELS, 
							    1);
  if(!output_struct->audio_samples_fifo){
    fprintf(stderr, "\nCould not open resample context\n");
    return 0;
  }

  //We are done
  return 1;
}


/* read a frame from the input stream and separates audio from video frames
   return the size of the data read
*/
 int readAFrame(input_data_struct* input_struct, output_data_struct* output_struct){

  int result = 0;
  int frameFinished;
  int decoded = 0;

  int input_video_stream_id    = input_struct->video_stream_id;
  int input_audio_stream_id    = input_struct->audio_stream_id;
  AVFormatContext *pFormatCtx  = input_struct->av_format_context;

  while(av_read_frame(pFormatCtx, &(input_struct->packet))>=0) {
    //The packet now contains some of our input stream data - we need to fill the correct fields

    // Is this a packet from the video stream?
    if(input_struct->packet.stream_index == input_video_stream_id) {

      /* Decode video frame - into the INPUT video frame holder
	 Note: this video frame has not been allocated by us, it's allocated and managed by avcodec_decode_video2
      */
      result = avcodec_decode_video2(input_struct->av_format_context->streams[input_video_stream_id]->codec,
				     input_struct->video_frame,
				     &frameFinished,
				     &(input_struct->packet));

      if(result <= 0){
	fprintf(stderr, "\nRead an Empty video frame!\n");
	//We need to keep track of the skipped video frames 
	output_struct->video_frames_skipped += input_struct->av_format_context->streams[input_video_stream_id]->codec->ticks_per_frame;
      }

      //Multiple av_read_frame calls can return a single frame
      if(frameFinished){
	//Now, input_struct->video_frame contains our frame, ready to be processed

	//A flag for the output (we can have both a video and an audio frame waiting)
	input_struct->has_video_frame = 1;
	
	/* The size of the data read */
	decoded = input_struct->packet.size;
	
	// We must free the packet before the next read.
	av_free_packet(&(input_struct->packet));
	return decoded;
      }

    }else if(input_struct->packet.stream_index == input_audio_stream_id) {
      //Decode the Audio Frame
      result = avcodec_decode_audio4(input_struct->av_format_context->streams[input_audio_stream_id]->codec,
				     input_struct->audio_frame,
				     &frameFinished,
				     &(input_struct->packet));


      /* Some audio decoders decode only part of the packet, and have to be
       * called again with the remainder of the packet data.
       * Sample: fate-suite/lossless-audio/luckynight-partial.shn
       * Also, some decoders might over-read the packet. 
       */
      /* The size of the data read */
      decoded = FFMIN(result, input_struct->packet.size);

      if(frameFinished){
	input_struct->has_audio_frame = 1;
	av_free_packet(&(input_struct->packet));
	return decoded;
      }
      

    }else{
      fprintf(stderr, "\nUnknown Stream Packet:  %d\n", input_struct->packet.stream_index);
      return 0;
    }

    // Free the packet that was allocated by av_read_frame
    av_free_packet(&(input_struct->packet));
  }
  return decoded;
}




/* The output_struct contains a frame which has to be consumed */
int saveVideoFrameOut(output_data_struct *output_struct, input_data_struct *input_struct){
  AVFrame *pFrameIn             = NULL;  
  AVFrame *pFrameOut            = NULL;  
  struct SwsContext *sws_ctx    = NULL;
  AVCodecContext *pCodecCtxRead = NULL;

  AVPacket  *pktPtr = NULL;

  int ret, got_output;


  
  //Push the video frame in to the video fifo buffer
  if(input_struct->video_frame_fifo_head < VIDEO_IN_FIFO_SIZE-1){
    input_struct->video_frame_fifo_head++;
    if(input_struct->video_frame_fifo_tail < 0){
      input_struct->video_frame_fifo_tail++;
    }
    input_struct->video_frame_fifo[input_struct->video_frame_fifo_head] = av_frame_clone(input_struct->video_frame);
    //The video frame has been consumed
    input_struct->has_video_frame = 0;
  }else{
    fprintf(stderr, "\nVideo Buffer overrun : %d  ==> %d \n", input_struct->video_frame_fifo_head , VIDEO_IN_FIFO_SIZE );
    return 0;
  }

  //printf("Video Audio Frame : [%d] %"PRIu64" %"PRIu64" \n", input_struct->video_frame_fifo_head,  output_struct->video_stream->cur_dts, output_struct->audio_stream->cur_dts);


  //printf("\nAudio Time Base: %d/%d \n", output_struct->audio_stream->time_base.num , output_struct->audio_stream->time_base.den);
  //printf("\nVideo Time Base: %d/%d \n", output_struct->video_stream->time_base.num , output_struct->video_stream->time_base.den);

  //printf("Video Duration: %"PRIu64" \n", output_struct->video_stream->cur_dts);
  //printf("Audio Duration: %"PRIu64" \n", output_struct->audio_stream->cur_dts);


  while( input_struct->video_frame_fifo_tail >=0 &&
	 (input_struct->video_frame_fifo_head - input_struct->video_frame_fifo_tail) >=0 
	 && ( output_struct->video_stream->cur_dts - output_struct->audio_stream->cur_dts) < AUDIO_VIDEO_MAX_DELAY){
    //The input frame - ready to be scaled
    /*
    printf("\nVideo Buffer Size Packed: %d T:%d H:%d\n", 
	   (input_struct->video_frame_fifo_head - input_struct->video_frame_fifo_tail),
	   input_struct->video_frame_fifo_tail,
	   input_struct->video_frame_fifo_head);

    */      
    pFrameIn = input_struct->video_frame_fifo[input_struct->video_frame_fifo_tail];
      
    //The output frame - already initialized with enought buffer memory 
    pFrameOut     = output_struct->video_frame;
    
    //The output packet, will hold the data for the output file
    pktPtr = &output_struct->packet;
    
    sws_ctx = output_struct->video_sws_ctx;
    ret = sws_scale(sws_ctx, 
		    (uint8_t const * const *)pFrameIn->data,
		    pFrameIn->linesize, 
		    0, 
		    pFrameIn->height,
		    pFrameOut->data, 
		    pFrameOut->linesize);
    
    
    if(ret != pFrameOut->height){
      fprintf(stderr, "\nError during frame scaling \n");
      return 0;
    }
    
    av_frame_free(&pFrameIn);
      
    input_struct->video_frame_fifo[input_struct->video_frame_fifo_tail] = NULL;
    input_struct->video_frame_fifo_tail++;
    
    if(input_struct->video_frame_fifo_tail == input_struct->video_frame_fifo_head){
      input_struct->video_frame_fifo[0] = input_struct->video_frame_fifo[input_struct->video_frame_fifo_tail];
      input_struct->video_frame_fifo_tail = 0;
      input_struct->video_frame_fifo_head = 0;
    }

    if(input_struct->video_frame_fifo_tail > input_struct->video_frame_fifo_head){    
      input_struct->video_frame_fifo_tail = -1;
      input_struct->video_frame_fifo_head = -1;
    }

      
      //Encode the output frame into the packet
      ret = avcodec_encode_video2(output_struct->video_stream->codec, 
				  pktPtr, 
				  pFrameOut, 
				  &got_output);
      
      
      if (ret < 0) {
	fprintf(stderr, "\nError encoding frame\n");
	return 0;
      }
      
      if (got_output) {
	
	/* Now, the frame has been converted in the output format 
	   BUT  we need to set it's presentation time pts
	   This is in the codec coded_frame->pts field
	   BUT again it has to be rescaled to the output stream time_base (which is different)
	   So:
	*/

	if (output_struct->video_stream->codec->coded_frame->pts != AV_NOPTS_VALUE){
	  //Here we add the skipped_frames_count, to keep track of the skipped frames, otherwise the audio will not be in sinc with video
	  pktPtr->pts= av_rescale_q(output_struct->video_stream->codec->coded_frame->pts + output_struct->video_frames_skipped*MAGIC_NUMBER_ALPHA , 
				    output_struct->video_stream->codec->time_base, 
				    output_struct->video_stream->time_base);
	  
	  pktPtr->dts = pktPtr->pts;
	}
	
	output_struct->video_pts = pktPtr->pts;
	
	//If is a key frame
	if(output_struct->video_stream->codec->coded_frame->key_frame)
	  pktPtr->flags |= AV_PKT_FLAG_KEY;
	
	pktPtr->stream_index  = output_struct->video_stream->index;

#if INTERLEAVED == 1	
	ret = av_interleaved_write_frame(output_struct->av_format_context, pktPtr);
#else
	ret = av_write_frame(output_struct->av_format_context, pktPtr);
#endif

	if(ret <0){
	  fprintf(stderr, "\nError saving audio frame\n");
	  return 0;
	}
	av_free_packet(pktPtr); 
      }
    }
  return 1;
}


/* The output_struct contains an audio frame which has to be consumed */ 
int saveAudioFrameOut(output_data_struct *output_struct, input_data_struct *input_struct){
  AVFrame *pFrameIn             = NULL;  
  AVFrame *pFrameOut            = NULL;  
  AVFrame *wFrameOut            = NULL;  

  SwrContext *audio_resample_ctx;

  AVPacket  *pktPtr = NULL;

  int ret, got_output;

  uint8_t *buffer            = NULL;
  int numBytes;

  //Input frame decoded from the input video file
  pFrameIn      = input_struct->audio_frame;

  //Output frame, will hold the resampled frame
  pFrameOut     = output_struct->audio_frame;

  /*Output Codec frame, this is the frame wich will be passed to the codec.
    The codec (when encoding) accepts (needs!) a number of samples bigger than the when decoding
  */
  wFrameOut     = output_struct->audio_codec_frame;

  //Output Packet, will hold the data
  pktPtr = &output_struct->packet;

  //Let's resample the audio output (no need for buffering , nb_samples out is big enough )
  audio_resample_ctx = output_struct->audio_resample_ctx;
  ret = swr_convert(audio_resample_ctx,
			   (uint8_t  **)pFrameOut->data,
			   AUDIO_OUT_SAMPLES_PER_FRAME,
			   (const uint8_t  **)pFrameIn->data,
 			   pFrameIn->nb_samples);


  if (ret < 0) {
    fprintf(stderr, "\nError when resampling audio packet\n");
    fprintf(stderr, "\nError String: %d %s \n", ret, get_error_text(ret));
    return(0);
  }
  fflush(stdout);  

  int result = 0;
  //Push the samples in the audio fifo
  result = av_audio_fifo_write(output_struct->audio_samples_fifo, (void **)pFrameOut->data,  pFrameOut->nb_samples);

  if(result < pFrameOut->nb_samples){
    fprintf(stderr, "\nCould not write data to FIFO\n");
    return 0;
  }

  //Ho consumato un frame
  input_struct->has_audio_frame = 0;
  

  //If we have enough samples in the fifo, we can send them to the encoder
  while (av_audio_fifo_size(output_struct->audio_samples_fifo) >= output_struct->audio_stream->codec->frame_size){

    av_audio_fifo_read(output_struct->audio_samples_fifo,
		       (void **)wFrameOut->data,
		       output_struct->audio_stream->codec->frame_size);

    //Set the pts
    wFrameOut->pts = output_struct->audio_samples_count + wFrameOut->nb_samples;
    output_struct->audio_samples_count += wFrameOut->nb_samples;


    
    got_output = 0;
    ret = avcodec_encode_audio2(output_struct->audio_stream->codec, pktPtr, wFrameOut, &got_output);

    /* encode the audio frame */
    if (ret < 0) {
      fprintf(stderr, "\nError encoding audio frame\n");
      fprintf(stderr, "\nError String: %d %s \n", ret, get_error_text(ret));
      return 0;
    }
    
    if (got_output) {

      //The packet pts has to be converted from the codec timebase to the stream time base
      av_packet_rescale_ts(pktPtr, output_struct->audio_stream->codec->time_base, output_struct->audio_stream->time_base);
      pktPtr->stream_index  = output_struct->audio_stream->index;

      output_struct->audio_pts = pktPtr->pts;
      //fprintf(stderr, "Video Pts - Audio Pts : %"PRIu64" \n", output_struct->video_pts - output_struct->audio_pts );
	  


      //Write to file
#if INTERLEAVED == 1	
      ret = av_interleaved_write_frame(output_struct->av_format_context, pktPtr);
#else
      ret = av_write_frame(output_struct->av_format_context, pktPtr);
#endif
      if(ret <0){
	fprintf(stderr, "\nError saving audio frame\n");
	return 0;
      }

      av_free_packet(pktPtr);
    }
  }
  

  return 1;
}

/* Tell if we have  a blackframe 
   It counts the number of black pixels in centered squares at the middle of the screen
*/
int isBlackFrame(AVFrame *pFrame){
  int Y,Cb,Cr;
  int frameAvg = 0;
  int64_t timestamp;

  int height,width;

  int result = 0;

  height = pFrame->height;
  width  = pFrame->width;

  if(pFrame->format == AV_PIX_FMT_YUV420P ){
    //The frame has to be in this format 

    frameAvg = countBlackPixels(pFrame, width/8, height/8, 20);
    if(frameAvg > 80){
      frameAvg = countBlackPixels(pFrame, width, height, 20);
    }
    
    //fprintf(stderr, "\nframeSum-frameAvg %d %d\n",frameSum,frameAvg);
    if(frameAvg > 80){
      result = 1;
    }
  }
  return result;
}


/* Counts the percentage of black pixels in the rectangle
   Depends to the pFrame format 
*/
int countBlackPixels(AVFrame *pFrame, int rectW, int rectH, int threshold){
  int x, y, halfX, halfY;
  int Y,Cb,Cr;
  int frameSum = 0;
  int frameAvg = 0;

  int width  = pFrame->width;
  int height = pFrame->height;

  int offsetX = (width-rectW)/2;
  int offsetY = (width-rectH)/2;

  char colors[3];
  int R,G,B;

  frameSum = 0;
  for(y=offsetY; y<rectH+offsetY; y++){
    for(x=offsetX; x<rectW+offsetX; x++){
      
      Y  = pFrame->data[0][y * pFrame->linesize[0] + x];

      halfX = x/2;
      halfY = y/2;
      Cb = pFrame->data[1][halfY * pFrame->linesize[1] + halfX];
      Cr = pFrame->data[2][halfY * pFrame->linesize[2] + halfX];

      if(Y < threshold){
	YUV2RGB(Y,Cr,Cb,colors);

	R = colors[0];
	G = colors[1];
	B = colors[2];

	if(R<18 && G<18 && B<18){
	  frameSum += 1;
	}
      }
    }
  }

  frameAvg = 100*frameSum/(rectW*rectH);
  
  return frameAvg;
    
}

void YUV2RGB(int Y, int Cr, int Cb, char *colors){
  int R,G,B;

      R = Y + 1.370705*(Cr-128);
      G = Y - 0.337633 * (Cb-128)-0.698001*(Cr-128);
      B = Y + 1.732446 * (Cb-128);

      if(R>255){R=255;  }
      if(G>255){G=255;  }
      if(B>255){B=255;  }
      
      if(R<0){R=0;  }
      if(G<0){G=0;  }
      if(B<0){B=0;  }

      R = R*220/256;
      G = G*220/256;
      B = B*220/256;

      colors[0] = R;
      colors[1] = G;
      colors[2] = B;
}

//Useful procedure for error handling and reporting
static const char *get_error_text(const int error){
  static char error_buffer[255];
  av_strerror(error, error_buffer, sizeof(error_buffer));
  return error_buffer;
}
