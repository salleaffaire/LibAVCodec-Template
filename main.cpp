#include <thread>
#include <mutex>
#include <iostream>
#include <fstream>
#include <list>
#include <vector>


extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

//#define INBUF_SIZE 4096

struct IDWConf {
   double             mDuration;
   unsigned long long mNumberOfFrames;
   unsigned long long mWMInterval;
};

int calculate_dwm_parameters(AVStream *avs, IDWConf *idwconf, unsigned long tmid_length) {
   
   const auto time_base = (double)avs->time_base.num / (double)avs->time_base.den;
   const auto duration = avs->duration * time_base;
   const auto nb_frames = avs->nb_frames;

   std::cout << "Time base : " << time_base << " secs."  << std::endl;
   std::cout << "Duration : " << duration << std::endl;
   std::cout << "Number of frames : " << nb_frames << std::endl;
   
   idwconf->mDuration = duration;
   idwconf->mNumberOfFrames = nb_frames;
   idwconf->mWMInterval = nb_frames / tmid_length;

   std::cout << "Watermarking interval = " <<  idwconf->mWMInterval << std::endl;
   std::cout << std::endl;
} 

int process(std::string filename) {
   int rval = 0;

   static std::once_flag initFlag;
   std::call_once(initFlag, []() {av_register_all(); });

   // Allocate AV Fomat Context
   std::shared_ptr<AVFormatContext> avFormat(avformat_alloc_context(), 
                                             &avformat_free_context);

   // Configuration Object
   IDWConf idwconf;
   
   // Try to open the file and parse the header
   auto avFormatPtr = avFormat.get();

   if (avformat_open_input(&avFormatPtr, filename.c_str(), nullptr, nullptr) == 0) {
      if (avformat_find_stream_info(avFormat.get(), nullptr) >= 0) {
         std::cout << "Opening: " << filename << std::endl;
         auto format = avFormat->iformat; 
         std::cout << format->long_name << std::endl;

         std::list<AVStream *> videoStreamList;
         for (unsigned int i=0; i < avFormat->nb_streams; ++i) {

            auto stream    = avFormat->streams[i];
            auto codecType = stream->codec->codec_type;
            auto codecID   = stream->codec->codec_id;
            auto width     = stream->codec->width;
            auto height    = stream->codec->height;

            if (AVMEDIA_TYPE_VIDEO == codecType) {
               std::cout << " Video stream - CODEC ID: " << codecID;
               if (codecID == AV_CODEC_ID_PRORES) {
                  std::cout << " is ProRes - supported" << std::endl;
                  std::cout << width << " x " << height << std::endl; 
                  videoStreamList.push_back(stream);
               }
               if (codecID == AV_CODEC_ID_MPEG2VIDEO) {
                  std::cout << " is MPEG2 - supported" << std::endl; 
                  videoStreamList.push_back(stream);
               }
            }
            else if (AVMEDIA_TYPE_AUDIO == codecType) {
               std::cout << " Audio stream - CODEC ID: " << codecID << std::endl;
            }
         }

         // Did we find a video stream to watermark ?
         if (videoStreamList.size() != 0) {
            for (auto it = videoStreamList.begin();it != videoStreamList.end();++it) {

               const auto codec_id = (*it)->codec->codec_id;
               const auto codec = avcodec_find_decoder(codec_id);  

               calculate_dwm_parameters(*it, &idwconf, 16);

               if (nullptr != codec) {
                  // Allocating codec structure
                  std::shared_ptr<AVCodecContext> 
                     avVideoCodec(avcodec_alloc_context3(codec), 
                                  [] (AVCodecContext *c) { avcodec_close(c); 
                                     av_free(c); }); 

                  if (AV_CODEC_ID_PRORES == codec_id) {
                     // For ProRes width and height must be set 
                     // Hardcoding for now 
                     /// TODO: Parse frame header 
                     avVideoCodec->width = (*it)->codec->width;
                     avVideoCodec->height = (*it)->codec->height;
                  }

                  //std::cout << "Width : " << avVideoCodec->width << std::endl;
                  //std::cout << "Height : " << avVideoCodec->height << std::endl;

                  // we need to make a copy of videoStream->codec->extradata 
                  // and give it to the context
                  // make sure that this vector exists as long as the 
                  // avVideoCodec exists
                  std::vector<unsigned char> 
                     codecContextExtraData((*it)->codec->extradata, 
                                           (*it)->codec->extradata + 
                                           (*it)->codec->extradata_size);

                  avVideoCodec->extradata = 
                     reinterpret_cast<unsigned char*>(codecContextExtraData.data());
                  avVideoCodec->extradata_size = codecContextExtraData.size();
                  
                  // Initializing the structure by opening the codec
                  if (avcodec_open2(avVideoCodec.get(), codec, nullptr) >= 0) {
                     // We have a decoder !!!
                     // Enter the decoding loop
                     // -----------------------------------------------------------------------------
                     // -----------------------------------------------------------------------------
                     std::shared_ptr<AVFrame> avFrame(av_frame_alloc(), &av_free);

                     AVPacket packet;
                     bool is_eof = false;
                     unsigned int packet_count = 0;
                     
                     do {
                        if (av_read_frame(avFormat.get(), &packet) >= 0) {
                           
                           // It is a packet from our video stream 
                           if ((*it)->index == packet.stream_index) {
                              int packet_lenght = packet.size;
                              std::cout << "Found a packet: " << packet_count++ 
                                        << " size = " << packet_lenght << std::endl;

                              int is_frame_available = 0;
                              const auto processedLenght = avcodec_decode_video2(avVideoCodec.get(),
                                                                                 avFrame.get(),
                                                                                 &is_frame_available,
                                                                                 &packet);

                              std::cout << "Processed lenght = " << processedLenght << std::endl;
                              if (processedLenght == packet_lenght) {
                                 if (is_frame_available) {
                                    std::cout << "Found a frame !" << std::endl;

                                    // Have a frame here in avFrame !!
                                    
                                    // Watermark HERE

                                    // Re-encode HERE

                                    // Store to intermediate format 
                                    
                                 }
                              }
                              
                           }
                           av_free_packet(&packet);
                        }
                        else {
                           is_eof = true;
                        }
                     } while (!is_eof);
                     
                     // -----------------------------------------------------------------------------
                     // ----------------------------------------------------------------------------- 
                  }
                  else {
                     std::cout << "Error initializing video codec " << std::endl;
                  }
               }
               else {
                  std::cout << "libavcodec didn't find the video codec" << std::endl;
               }
                  
            }
         }
         else {
            std::cout << "No suitable video stream to watermark." << std::endl;
         }

      }
      else {
         std::cout << "Error calling avformat_find_stream_info" << std::endl;
         rval = -1;
      }
   }
   else {
      std::cout << "Error calling avformat_open_input " << std::endl
                << "Possible invalid file format" << std::endl; 
      rval = -1;
   }
   
   return rval;
}

int 
main(int argc, char *argv[]) {

   std::string filename = "prores422_lt_29.97.mov";
   //std::string filename = "xdcamhd-50mbps-1080i50.mxf";

   // Launching a file process
   process(filename);
   

}
