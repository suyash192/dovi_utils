#include <stdio.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>

int main(int argc, char *argv[]) {
    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;
    AVPacket *pkt = NULL;
    AVPacket *out_pkt = NULL;
    int ret;

    // --- 1. Setup Input Demuxer (For perfect MKV headers) ---
    const AVInputFormat *in_fmt = av_find_input_format("hevc");
    AVDictionary *in_opts = NULL;
    av_dict_set(&in_opts, "framerate", "24000/1001", 0);

    if ((ret = avformat_open_input(&ifmt_ctx, "pipe:0", in_fmt, &in_opts)) < 0) {
        fprintf(stderr, "Error opening stdin: %s\n", av_err2str(ret));
        return 1;
    }
    av_dict_free(&in_opts);

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        fprintf(stderr, "Error finding stream info.\n");
        return 1;
    }

    // --- 2. Setup Manual Parser (To extract the POC) ---
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    AVCodecParserContext *parser = av_parser_init(codec->id);
    AVCodecContext *parser_ctx = avcodec_alloc_context3(codec);
    if (!parser || !parser_ctx) {
        fprintf(stderr, "Failed to initialize manual HEVC parser.\n");
        return 1;
    }

    // --- 3. Setup MKV Output ---
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "matroska", "pipe:1");
    AVStream *in_stream = ifmt_ctx->streams[0];
    AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);

    avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    out_stream->codecpar->codec_tag = 0;

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, "pipe:1", AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Error opening stdout.\n");
            return 1;
        }
    }

    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        fprintf(stderr, "Error writing MKV header.\n");
        return 1;
    }

    // --- 4. Packet Loop & POC Math ---
    pkt = av_packet_alloc();
    out_pkt = av_packet_alloc();

    int64_t dts_counter = 0;
    int64_t pts_offset = 0;
    const int MAX_B_FRAMES = 16;
    AVRational raw_time_base = {1001, 24000};

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        uint8_t *data = pkt->data;
        int size = pkt->size;

        while (size > 0) {
            uint8_t *poutbuf = NULL;
            int poutbuf_size = 0;

            int len = av_parser_parse2(parser, parser_ctx, &poutbuf, &poutbuf_size,
                                       data, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (len < 0) break;

            data += len;
            size -= len;

            if (poutbuf_size > 0) {
                out_pkt->data = poutbuf;
                out_pkt->size = poutbuf_size;
                out_pkt->flags = 0; // Reset flags for the new frame

                int64_t poc = parser->output_picture_number;

                // --- THE FIX: Mark Keyframes for MKV Indexing ---
                if (parser->key_frame == 1) {
                    out_pkt->flags |= AV_PKT_FLAG_KEY;
                    if (poc != AV_NOPTS_VALUE) {
                        pts_offset = dts_counter - poc;
                    }
                }

                if (poc != AV_NOPTS_VALUE) {
                    out_pkt->pts = poc + pts_offset + MAX_B_FRAMES;
                } else {
                    out_pkt->pts = dts_counter + MAX_B_FRAMES;
                }

                out_pkt->dts = dts_counter;
                out_pkt->duration = 1;

                av_packet_rescale_ts(out_pkt, raw_time_base, out_stream->time_base);
                out_pkt->stream_index = out_stream->index;

                if (av_interleaved_write_frame(ofmt_ctx, out_pkt) < 0) {
                    fprintf(stderr, "Error writing frame.\n");
                }

                dts_counter++;
            }
        }
        av_packet_unref(pkt);
    }

    // --- 5. Flush the Parser ---
    uint8_t *poutbuf = NULL;
    int poutbuf_size = 0;
    av_parser_parse2(parser, parser_ctx, &poutbuf, &poutbuf_size,
                     NULL, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

    if (poutbuf_size > 0) {
        out_pkt->data = poutbuf;
        out_pkt->size = poutbuf_size;
        out_pkt->flags = 0;

        int64_t poc = parser->output_picture_number;
        if (parser->key_frame == 1) {
            out_pkt->flags |= AV_PKT_FLAG_KEY;
            if (poc != AV_NOPTS_VALUE) pts_offset = dts_counter - poc;
        }

        if (poc != AV_NOPTS_VALUE) {
            out_pkt->pts = poc + pts_offset + MAX_B_FRAMES;
        } else {
            out_pkt->pts = dts_counter + MAX_B_FRAMES;
        }

        out_pkt->dts = dts_counter;
        out_pkt->duration = 1;

        av_packet_rescale_ts(out_pkt, raw_time_base, out_stream->time_base);
        out_pkt->stream_index = out_stream->index;
        av_interleaved_write_frame(ofmt_ctx, out_pkt);
    }

    // --- 6. Cleanup ---
    av_write_trailer(ofmt_ctx);

    av_packet_free(&pkt);
    av_packet_free(&out_pkt);
    av_parser_close(parser);
    avcodec_free_context(&parser_ctx);
    avformat_close_input(&ifmt_ctx);
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);

    return 0;
}