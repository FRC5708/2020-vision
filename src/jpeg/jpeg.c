// Adaped by Team 5708 from https://github.com/6by9/userland/blob/hello_mmal/host_applications/linux/apps/hello_pi/hello_mmal_jpeg/jpeg.c

/*
Copyright (c) 2018 Raspberry Pi (Trading) Ltd

Decodes a JPEG image into a memory buffer.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

//#include <interface/vcsm/user-vcsm.h>
#include <bcm_host.h>
#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_connection.h>
#include <interface/mmal/mmal_queue.h>
#include <interface/vcos/vcos.h>

#define MAX_BUFFERS 2

static inline int warn(const char *file, int line, const char *fmt, ...)
{
   int errsv = errno;
   va_list va;
   va_start(va, fmt);
   fprintf(stderr, "WARN(%s:%d): ", file, line);
   vfprintf(stderr, fmt, va);
   va_end(va);
   errno = errsv;
   return 1;
}

#define CHECK_CONDITION(cond, ...) \
do { \
   if (cond) { \
      int errsv = errno; \
      fprintf(stderr, "ERROR(%s:%d) : ", \
         __FILE__, __LINE__); \
      errno = errsv; \
      fprintf(stderr,  __VA_ARGS__); \
      abort(); \
   } \
} while(0)
#define WARN_ON(cond, ...) \
   ((cond) ? warn(__FILE__, __LINE__, __VA_ARGS__) : 0)
#define ERRSTR strerror(errno)
#define CHECK_STATUS(status, ...) WARN_ON(status != MMAL_SUCCESS, __VA_ARGS__); \
   if (status != MMAL_SUCCESS){ fprintf(stderr, "Error: %i\n", status); goto error; }

//static FILE *source_file;

/* Macros abstracting the I/O, just to make the example code clearer */
#define SOURCE_OPEN(uri) \
   source_file = fopen(uri, "rb"); if (!source_file) goto error;
#define SOURCE_READ_CODEC_CONFIG_DATA(bytes, size) \
   size = fread(bytes, 1, size, source_file); rewind(source_file)
#define SOURCE_READ_DATA_INTO_BUFFER(a) \
   a->length = fread(a->data, 1, a->alloc_size - 128, source_file); \
   a->offset = 0
#define SOURCE_CLOSE() \
   if (source_file) fclose(source_file)

/** Context for our application */
static struct CONTEXT_T {
   VCOS_SEMAPHORE_T semaphore;
   MMAL_QUEUE_T *queue;
   MMAL_STATUS_T status;
} context;

static void log_format(MMAL_ES_FORMAT_T *format, MMAL_PORT_T *port)
{
   const char *name_type;

   if(port)
      fprintf(stderr, "%s:%s:%i", port->component->name,
               port->type == MMAL_PORT_TYPE_CONTROL ? "ctr" :
                  port->type == MMAL_PORT_TYPE_INPUT ? "in" :
                  port->type == MMAL_PORT_TYPE_OUTPUT ? "out" : "invalid",
               (int)port->index);

   switch(format->type)
   {
   case MMAL_ES_TYPE_AUDIO: name_type = "audio"; break;
   case MMAL_ES_TYPE_VIDEO: name_type = "video"; break;
   case MMAL_ES_TYPE_SUBPICTURE: name_type = "subpicture"; break;
   default: name_type = "unknown"; break;
   }

   fprintf(stderr, "type: %s, fourcc: %4.4s\n", name_type, (char *)&format->encoding);
   fprintf(stderr, " bitrate: %i, framed: %i\n", format->bitrate,
            !!(format->flags & MMAL_ES_FORMAT_FLAG_FRAMED));
   fprintf(stderr, " extra data: %i, %p\n", format->extradata_size, format->extradata);
   switch(format->type)
   {
   case MMAL_ES_TYPE_AUDIO:
      fprintf(stderr, " samplerate: %i, channels: %i, bps: %i, block align: %i\n",
               format->es->audio.sample_rate, format->es->audio.channels,
               format->es->audio.bits_per_sample, format->es->audio.block_align);
      break;

   case MMAL_ES_TYPE_VIDEO:
      fprintf(stderr, " width: %i, height: %i, (%i,%i,%i,%i)\n",
               format->es->video.width, format->es->video.height,
               format->es->video.crop.x, format->es->video.crop.y,
               format->es->video.crop.width, format->es->video.crop.height);
      fprintf(stderr, " pixel aspect ratio: %i/%i, frame rate: %i/%i\n",
               format->es->video.par.num, format->es->video.par.den,
               format->es->video.frame_rate.num, format->es->video.frame_rate.den);
      break;

   case MMAL_ES_TYPE_SUBPICTURE:
      break;

   default: break;
   }

   if(!port)
      return;

   fprintf(stderr, " buffers num: %i(opt %i, min %i), size: %i(opt %i, min: %i), align: %i\n",
            port->buffer_num, port->buffer_num_recommended, port->buffer_num_min,
            port->buffer_size, port->buffer_size_recommended, port->buffer_size_min,
            port->buffer_alignment_min);
}

/** Callback from the control port.
 * Component is sending us an event. */
static void control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   struct CONTEXT_T *ctx = (struct CONTEXT_T *)port->userdata;

   switch (buffer->cmd)
   {
   case MMAL_EVENT_EOS:
      /* Only sink component generate EOS events */
      break;
   case MMAL_EVENT_ERROR:
      /* Something went wrong. Signal this to the application */
      ctx->status = *(MMAL_STATUS_T *)buffer->data;
      break;
   default:
      break;
   }

   /* Done with the event, recycle it */
   mmal_buffer_header_release(buffer);

   /* Kick the processing thread */
   vcos_semaphore_post(&ctx->semaphore);
}

/** Callback from the input port.
 * Buffer has been consumed and is available to be used again. */
static void input_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   struct CONTEXT_T *ctx = (struct CONTEXT_T *)port->userdata;

   /* The decoder is done with the data, just recycle the buffer header into its pool */
   mmal_buffer_header_release(buffer);

   /* Kick the processing thread */
   vcos_semaphore_post(&ctx->semaphore);
}

/** Callback from the output port.
 * Buffer has been produced by the port and is available for processing. */
static void output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   struct CONTEXT_T *ctx = (struct CONTEXT_T *)port->userdata;

   /* Queue the decoded video frame */
   mmal_queue_put(ctx->queue, buffer);

   /* Kick the processing thread */
   vcos_semaphore_post(&ctx->semaphore);
}

//int main(int argc, char **argv)

// get_data acts like fread. It is called multiple times until all the input data for a frame is gotten.
// It should hang if there is no more data at this moment and return 0 if the stream has ended.
// decoded_buffer is called with the raw decoded video (the whole thing).
typedef struct userptr_t userptr_t;
void decode_jpeg_video(userptr_t* userptr, size_t (*get_data)(void* buf, size_t size, userptr_t* user), void (*decoded_buffer)(void* buf, size_t length, userptr_t* user))
{
   MMAL_STATUS_T status = MMAL_EINVAL;
   MMAL_COMPONENT_T *decoder = NULL;
   MMAL_POOL_T *pool_in = NULL, *pool_out = NULL;
   MMAL_BOOL_T eos_sent = MMAL_FALSE, eos_received = MMAL_FALSE;
   unsigned int in_count = 0, out_count = 0;
   MMAL_BUFFER_HEADER_T *buffer;

   /*if (argc < 2)
   {
      fprintf(stderr, "invalid arguments\n");
      return -1;
   }*/

   bcm_host_init();

//   vcsm_init();

   vcos_semaphore_create(&context.semaphore, "example", 1);

   //SOURCE_OPEN(argv[1]);

   /* Create the decoder component.
    * This specific component exposes 2 ports (1 input and 1 output). Like most components
    * its expects the format of its input port to be set by the client in order for it to
    * know what kind of data it will be fed. */
   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_DECODER, &decoder);
   CHECK_STATUS(status, "failed to create decoder");

   /* Enable control port so we can receive events from the component */
   decoder->control->userdata = (void *)&context;
   status = mmal_port_enable(decoder->control, control_callback);
   CHECK_STATUS(status, "failed to enable control port");

   /* Set the zero-copy parameter on the input port */
//   status = mmal_port_parameter_set_boolean(decoder->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
//   CHECK_STATUS(status, "failed to set zero copy - %s", decoder->input[0]->name);

   /* Set the zero-copy parameter on the output port */
   status = mmal_port_parameter_set_boolean(decoder->output[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
   CHECK_STATUS(status, "failed to set zero copy - %s", decoder->output[0]->name);

   /* Set format of video decoder input port */
   MMAL_ES_FORMAT_T *format_in = decoder->input[0]->format;
   format_in->type = MMAL_ES_TYPE_VIDEO;
   format_in->encoding = MMAL_ENCODING_JPEG;
   format_in->es->video.width = 0;
   format_in->es->video.height = 0;
   //format_in->es->video.width = 800;
   //format_in->es->video.height = 448;
   //format_in->es->video.crop = (MMAL_RECT_T) { 0, 0, 800, 448 };

   format_in->es->video.frame_rate.num = 0;
   format_in->es->video.frame_rate.den = 1;
   format_in->es->video.par.num = 1;
   format_in->es->video.par.den = 1;

   status = mmal_port_format_commit(decoder->input[0]);

   //fprintf(stderr, "committing input format: ");
   //log_format(decoder->input[0]->format, decoder->input[0]);
   CHECK_STATUS(status, "failed to commit input format\n");

   MMAL_ES_FORMAT_T *format_out = decoder->output[0]->format;
   format_out->encoding = MMAL_ENCODING_I420;
   //format_out->encoding = MMAL_ENCODING_I422;

   status = mmal_port_format_commit(decoder->output[0]);
   CHECK_STATUS(status, "failed to commit output format\n");

   /* Display the output port format */
   fprintf(stderr, "%s\n", decoder->output[0]->name);
   fprintf(stderr, " type: %i, fourcc: %4.4s\n", format_out->type, (char *)&format_out->encoding);
   fprintf(stderr, " bitrate: %i, framed: %i\n", format_out->bitrate,
           !!(format_out->flags & MMAL_ES_FORMAT_FLAG_FRAMED));
   fprintf(stderr, " extra data: %i, %p\n", format_out->extradata_size, format_out->extradata);
   fprintf(stderr, " width: %i, height: %i, (%i,%i,%i,%i)\n",
           format_out->es->video.width, format_out->es->video.height,
           format_out->es->video.crop.x, format_out->es->video.crop.y,
           format_out->es->video.crop.width, format_out->es->video.crop.height);

   /* The format of both ports is now set so we can get their buffer requirements and create
    * our buffer headers. We use the buffer pool API to create these. */
   decoder->input[0]->buffer_num = decoder->input[0]->buffer_num_recommended;
   decoder->input[0]->buffer_size = decoder->input[0]->buffer_size_recommended;
   decoder->output[0]->buffer_num = decoder->output[0]->buffer_num_recommended;
   decoder->output[0]->buffer_size = decoder->output[0]->buffer_size_recommended;
   pool_in = mmal_port_pool_create(decoder->input[0],
                                   decoder->input[0]->buffer_num,
                                   decoder->input[0]->buffer_size);

   /* Create a queue to store our decoded frame(s). The callback we will get when
    * a frame has been decoded will put the frame into this queue. */
   context.queue = mmal_queue_create();

   /* Store a reference to our context in each port (will be used during callbacks) */
   decoder->input[0]->userdata = (void *)&context;
   decoder->output[0]->userdata = (void *)&context;

   /* Enable all the input port and the output port.
    * The callback specified here is the function which will be called when the buffer header
    * we sent to the component has been processed. */
   status = mmal_port_enable(decoder->input[0], input_callback);
   CHECK_STATUS(status, "failed to enable input port");

   status = mmal_port_enable(decoder->output[0], output_callback);
   CHECK_STATUS(status, "failed to enable output port");

   pool_out = mmal_port_pool_create(decoder->output[0],
                                decoder->output[0]->buffer_num,
                                decoder->output[0]->buffer_size);

   while ((buffer = mmal_queue_get(pool_out->queue)) != NULL)
   {
      //printf("Sending buf %p\n", buffer);
      status = mmal_port_send_buffer(decoder->output[0], buffer);
      CHECK_STATUS(status, "failed to send output buffer to decoder");
   }

   /* Component won't start processing data until it is enabled. */
   status = mmal_component_enable(decoder);
   CHECK_STATUS(status, "failed to enable decoder component");

   /* Start decoding */
   fprintf(stderr, "start decoding\n");

   /* This is the main processing loop */
   while(!eos_received || !eos_sent)
   {
      VCOS_STATUS_T vcos_status;

      /* Wait for buffer headers to be available on either of the decoder ports */
      vcos_status = vcos_semaphore_wait_timeout(&context.semaphore, 2000);
      if (vcos_status != VCOS_SUCCESS)
         fprintf(stderr, "vcos_semaphore_wait_timeout failed - status %d\n", vcos_status);

      /* Check for errors */
      if (context.status != MMAL_SUCCESS) {
         fprintf(stderr, "MMAL error %i, look in mmal_types.h\n", context.status);
         break;
      }

      /* Send data to decode to the input port of the video decoder */
      if (!eos_sent && (buffer = mmal_queue_get(pool_in->queue)) != NULL)
      {
         //SOURCE_READ_DATA_INTO_BUFFER(buffer);
         buffer->length = get_data(buffer->data, buffer->alloc_size - 128, userptr);
         buffer->offset = 0;

         if(!buffer->length) eos_sent = MMAL_TRUE;

         buffer->flags = buffer->length ? 0 : MMAL_BUFFER_HEADER_FLAG_EOS;
         buffer->pts = buffer->dts = MMAL_TIME_UNKNOWN;
         fprintf(stderr, "sending %i bytes\n", (int)buffer->length);
         status = mmal_port_send_buffer(decoder->input[0], buffer);
         CHECK_STATUS(status, "failed to send buffer");
         in_count++;
         fprintf(stderr, "Input buffer %p to port %s. in_count %u\n", buffer, decoder->input[0]->name, in_count);
      }

      //fprintf(stderr, "input port format: ");
      //log_format(decoder->input[0]->format, decoder->input[0]);
      //fprintf(stderr, "output port format: ");
      //log_format(decoder->output[0]->format, decoder->output[0]);

      /* Get our output frames */
      while ((buffer = mmal_queue_get(context.queue)) != NULL)
      {
         /* We have a frame, do something with it (why not display it for instance?).
          * Once we're done with it, we release it. It will automatically go back
          * to its original pool so it can be reused for a new video frame.
          */
         eos_received = buffer->flags & MMAL_BUFFER_HEADER_FLAG_EOS;

         if (buffer->cmd)
         {
            fprintf(stderr, "received event length %d, %4.4s\n", buffer->length, (char *)&buffer->cmd);
            if (buffer->cmd == MMAL_EVENT_FORMAT_CHANGED)
            {
               MMAL_EVENT_FORMAT_CHANGED_T *event = mmal_event_format_changed_get(buffer);
               if (event)
               {
                  fprintf(stderr, "----------Port format changed----------\n");
                  log_format(decoder->output[0]->format, decoder->output[0]);
                  fprintf(stderr, "-----------------to---------------------\n");
                  log_format(event->format, 0);
                  fprintf(stderr, " buffers num (opt %i, min %i), size (opt %i, min: %i)\n",
                           event->buffer_num_recommended, event->buffer_num_min,
                           event->buffer_size_recommended, event->buffer_size_min);
                  fprintf(stderr, "----------------------------------------\n");
               }
               mmal_buffer_header_release(buffer);
               mmal_port_disable(decoder->output[0]);

               //Clear out the queue and release the buffers.
               while(mmal_queue_length(pool_out->queue) < pool_out->headers_num)
               {
                  buffer = mmal_queue_wait(context.queue);
                  mmal_buffer_header_release(buffer);
                  fprintf(stderr, "Retrieved buffer %p\n", buffer);
               }

               //Assume we can't reuse the output buffers, so have to disable, destroy
               //pool, create new pool, enable port, feed in buffers.
               mmal_port_pool_destroy(decoder->output[0], pool_out);

               status = mmal_format_full_copy(decoder->output[0]->format, event->format);
               decoder->output[0]->format->encoding = MMAL_ENCODING_I420;
               decoder->output[0]->buffer_num = MAX_BUFFERS;
               decoder->output[0]->buffer_size = decoder->output[0]->buffer_size_recommended;

               if (status == MMAL_SUCCESS)
                  status = mmal_port_format_commit(decoder->output[0]);
               if (status != MMAL_SUCCESS)
               {
                  fprintf(stderr, "commit failed on output - %d\n", status);
               }

               mmal_port_enable(decoder->output[0], output_callback);
               pool_out = mmal_port_pool_create(decoder->output[0], decoder->output[0]->buffer_num, decoder->output[0]->buffer_size);
            }
            else
            {
               mmal_buffer_header_release(buffer);
            }
            continue;
         }
         else
         {
            fprintf(stderr, "decoded frame (flags %x, size %d) count %d\n", buffer->flags, buffer->length, out_count);

            // Do something here with the content of the buffer

            decoded_buffer(buffer->data, buffer->length, userptr);

            mmal_buffer_header_release(buffer);

            out_count++;
         }
      }

      /* Send empty buffers to the output port of the decoder */
      while ((buffer = mmal_queue_get(pool_out->queue)) != NULL)
      {
         //printf("Sending buf %p\n", buffer);
         status = mmal_port_send_buffer(decoder->output[0], buffer);
         CHECK_STATUS(status, "failed to send output buffer to decoder");
      }
   }

   /* Stop decoding */
   fprintf(stderr, "stop decoding\n");

   /* Stop everything. Not strictly necessary since mmal_component_destroy()
    * will do that anyway */
   mmal_port_disable(decoder->input[0]);
   mmal_port_disable(decoder->output[0]);
   mmal_component_disable(decoder);

 error:
   /* Cleanup everything */
   if (pool_in)
      mmal_port_pool_destroy(decoder->input[0], pool_in);
   if (pool_out)
      mmal_port_pool_destroy(decoder->output[0], pool_out);
   if (decoder)
      mmal_component_destroy(decoder);
   if (context.queue)
      mmal_queue_destroy(context.queue);

   //SOURCE_CLOSE();
   vcos_semaphore_delete(&context.semaphore);
   //return status == MMAL_SUCCESS ? 0 : -1;
}
