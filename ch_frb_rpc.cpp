#include <iostream>
#include <sstream>
#include <pthread.h>
#include <ch_frb_io.hpp>
#include <ch_frb_rpc.hpp>
#include <unistd.h>

//  ZeroMQ experiment, from "Hello World server in C++"
//  Binds REP socket to tcp://*:5555
//
#include <zmq.hpp>
#include <string>

// msgpack
#include <msgpack.hpp>

// protobuf
#include <rpc.pb.h>

using namespace std;

namespace pb = ::google::protobuf;

frb_rpc_server::frb_rpc_server(std::shared_ptr<intensity_network_stream> s) :
    stream(s)
{
}

frb_rpc_server::~frb_rpc_server() {}

struct rpc_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
};

/**

 RPC calls:

 - [dict] get_beam_metadata(void)
 ---> returns an array of dictionaries? describing the beams handled by this
      L1 node.  Elements would include:
        int beam_id
        int nupfreq
        int nt_per_packet
        int fpga_counts_per_sample
        constants like nt_per_assembled_chunk, nfreq_coarse?
        int64 fpga_count // first fpga count seen; 0 if no L0 packets seen yet
        int ring buffer capacity?
        int ring buffer size?
        int64 min_fpga_count // in ring buffer
        int64 max_fpga_count // in ring buffer
        <packet count statistics>
        <packet counts from each L0 node>

 - [intensity_packet] get_packets([
          ( beam_id,
            // high index non-inclusive; 0 means max (no cut)
            fpga_count_low, fpga_count_high,
            freq_coarse_low, freq_coarse_high,
            upfreq_low, upfreq_high,
            tsamp_low, tsamp_high )
          ])
 ---> returns an array of intensity_packets (or sub-packets).

 - [bool] dump_packets(...)
 ---> to disk?

 */




static void *rpc_thread_main(void *opaque_arg) {
    rpc_thread_context *context = reinterpret_cast<rpc_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    delete context;

    cout << "Hello, I am the RPC thread" << endl;

    // ZMQ
    //  Prepare our context and socket
    zmq::context_t zcontext(1);
    zmq::socket_t socket(zcontext, ZMQ_REP);
    socket.bind("tcp://*:5555");

    while (true) {
        zmq::message_t request;

        //  Wait for next request from client
        socket.recv(&request);
        std::cout << "Received RPC request" << std::endl;

        // Parse RPC header.
        RpcRequestHeader hdr;
        hdr.ParseFromArray(request.data(), request.size());
        string func = hdr.funcname();

        // Assume the function arguments follow in a second ZMQ message
        zmq::message_t body;
        socket.recv(&body);

        cout << "Request for function name " << func << endl;
        cout << "Request args size: " << body.size() << endl;

        if (func == "GetBeamMetadata") {
            cout << "getBeamMetadata() request" << endl;
            GetBeamMetadata_Request req;
            req.ParseFromArray(body.data(), body.size());

            // The actual underlying call...
            std::vector<
                std::unordered_map<std::string, uint64_t> > R =
                stream->get_statistics();

            GetBeamMetadata_Response res;
            pb::Map<string, pb::uint64>* m = res.mutable_metadata();
            /// ugh, deep copy required
            m->insert(R[0].begin(), R[0].end());

            int nbeams = R.size() - 1;
            //pb::RepeatedPtrField<GetBeamMetadata_Response_PerBeamMetadata>* beams = res.mutable_beams();
            auto* beams = res.mutable_beams();
            for (int i=0; i<nbeams; i++) {
                //GetBeamMetadata_Response_PerBeamMetadata* pbm = beams->Add();
                //pbm->mutable_metadata()->insert(R[i+1].begin(), R[i+1].end());
                beams->Add()->mutable_metadata()->insert(
                                   R[i+1].begin(), R[i+1].end());
            }

            string replystr = res.SerializeAsString();
            zmq::message_t reply(reinterpret_cast<const void*>(replystr.data()),
                                 replystr.size());
            socket.send(reply);

        } else if (func == "GetChunks") {
            cout << "GetChunks() request" << endl;
            GetChunks_Request req;
            req.ParseFromArray(body.data(), body.size());
            GetChunks_Response res;

            // requested chunks
            uint64_t minchunk = req.min_chunk();
            uint64_t maxchunk = req.max_chunk();

            // requested beams... deep copy to std::vector
            std::vector<uint64_t> beams;
            int nbeams = req.beam_size();
            for (int i=0; i<nbeams; i++) {
                uint64_t beam = req.beam(i);
                beams.push_back(beam);
            }

            vector<vector<shared_ptr<assembled_chunk> > > snaps = stream->get_ringbuf_snapshots(beams);
            // iterate over beams
            for (vector<vector<shared_ptr<assembled_chunk> > >::iterator
                     it = snaps.begin(); it != snaps.end(); it++) {
                // iterate over chunks
                for (vector<shared_ptr<assembled_chunk> >::iterator
                         chunkit = (*it).begin(); chunkit != (*it).end();
                     chunkit++) {
                    shared_ptr<assembled_chunk> chunk = *chunkit;
                    // If desired chunk range was specified, apply cut.
                    if (minchunk && (chunk->ichunk < minchunk))
                        continue;
                    if (maxchunk && (chunk->ichunk > maxchunk))
                        continue;

                    Chunk* ch = res.mutable_chunks()->Add();
                    ch->set_beam_id(chunk->beam_id);
                    ch->set_nupfreq(chunk->nupfreq);
                    ch->set_nt_per_packet(chunk->nt_per_packet);
                    ch->set_fpga_counts_per_sample(chunk->fpga_counts_per_sample);
                    ch->set_nt_coarse(chunk->nt_coarse);
                    ch->set_nscales(chunk->nscales);
                    ch->set_ndata(chunk->ndata);
                    ch->set_ichunk(chunk->ichunk);
                    ch->set_isample(chunk->isample);

                    for (int j=0; j<chunk->nscales; j++) {
                        ch->add_scales(chunk->scales[j]);
                    }
                    for (int j=0; j<chunk->nscales; j++) {
                        ch->add_offsets(chunk->offsets[j]);
                    }

                    ch->set_data(chunk->data, chunk->ndata);
                }
            }

            string replystr = res.SerializeAsString();
            zmq::message_t reply(reinterpret_cast<const void*>(replystr.data()),
                                 replystr.size());
            socket.send(reply);
        }
    }

    return NULL;
}

void frb_rpc_server::start() {
    cout << "Starting RPC server..." << endl;

    rpc_thread_context *context = new rpc_thread_context;
    context->stream = stream;

    int err = pthread_create(&rpc_thread, NULL, rpc_thread_main, context);
    if (err)
        throw runtime_error(string("pthread_create() failed to create RPC thread: ") + strerror(errno));
    
}

void frb_rpc_server::stop() {
    cout << "Stopping RPC server..." << endl;
}


