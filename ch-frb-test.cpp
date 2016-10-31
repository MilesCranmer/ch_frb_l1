/*
 * 
 */
#include <cassert>
#include <iostream>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ch_frb_io.hpp>
#include <ch_frb_rpc.hpp>

#include <msgpack.hpp>
#include <zmq.hpp>

#if defined(__AVX2__)
const static bool HAVE_AVX2 = true;
#else
#warning "This machine does not have the AVX2 instruction set."
const static bool HAVE_AVX2 = false;
#endif

using namespace std;

struct processing_thread_context {
    shared_ptr<ch_frb_io::intensity_network_stream> stream;
    int ithread = -1;
};

static void *processing_thread_main(void *opaque_arg)
{
    const int nupfreq = 16;
    const int nalloc = ch_frb_io::constants::nfreq_coarse_tot * nupfreq * ch_frb_io::constants::nt_per_assembled_chunk;

    std::vector<float> intensity(nalloc, 0.0);
    std::vector<float> weights(nalloc, 0.0);

    processing_thread_context *context = reinterpret_cast<processing_thread_context *> (opaque_arg);
    shared_ptr<ch_frb_io::intensity_network_stream> stream = context->stream;
    int ithread = context->ithread;
    delete context;

    for (;;) {
        cout << "Processing thread " << ithread << " waiting for chunks" << endl;
	// Get assembled data from netwrok
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk) {
        cout << "Processing thread: found end-of-stream" << endl;
	    break;  // End-of-stream reached
    }

	assert(chunk->nupfreq == nupfreq);

	// We call assembled_chunk::decode(), which extracts the data from its low-level 8-bit
	// representation to a floating-point array, but our processing currently stops there!
	chunk->decode(&intensity[0], &weights[0], ch_frb_io::constants::nt_per_assembled_chunk);
    cout << "Decoded beam " << chunk->beam_id << ", chunk " << chunk->ichunk << endl;
    }
    return NULL;
}

static void spawn_processing_thread(pthread_t &thread, const shared_ptr<ch_frb_io::intensity_network_stream> &stream, int ithread)
{
    processing_thread_context *context = new processing_thread_context;
    context->stream = stream;
    context->ithread = ithread;

    int err = pthread_create(&thread, NULL, processing_thread_main, context);
    if (err)
	throw runtime_error(string("pthread_create() failed to create processing thread: ") + strerror(errno));

    // no "delete context"!
}

/*
 * 
 */
int main(int argc, char** argv) {

    const int n_l1_nodes = 4;
    const int n_beams_per_l1_node = 4;
    const int n_l0_nodes = 8;

    const int n_coarse_freq_per_l0 = constants::nfreq_coarse_tot / n_l0_nodes;

    //int nchunks = int(gb_to_simulate * 1.0e9 / ostream->nbytes_per_chunk) + 1;
    int nchunks = 2;

    const int udp_port_l1_base = 10255;
    const int rpc_port_l1_base = 5555;

    vector<shared_ptr<intensity_network_stream> > l1streams;
    // Spawn one processing thread per beam
    pthread_t processing_threads[n_l1_nodes * n_beams_per_l1_node];
    vector<shared_ptr<frb_rpc_server> > rpcs;
    vector<string> rpc_ports;

    vector<vector<shared_ptr<intensity_network_ostream> > > l0streams;

    for (int i=0; i<n_l1_nodes; i++) {
        ch_frb_io::intensity_network_stream::initializer ini_params;
        for (int j=0; j<n_beams_per_l1_node; j++) {
            ini_params.beam_ids.push_back(i*n_beams_per_l1_node + j);
        }
        ini_params.mandate_fast_kernels = HAVE_AVX2;
        int udp_port = udp_port_l1_base + i;
        ini_params.udp_port = udp_port;
        cout << "Starting L1 node listening on UDP port " << udp_port << endl;

        shared_ptr<intensity_network_stream> stream = 
            ch_frb_io::intensity_network_stream::make(ini_params);

        // Spawn one processing thread per beam
        for (int j=0; j<n_beams_per_l1_node; j++) {
            int ibeam = i*n_beams_per_l1_node + j;
            spawn_processing_thread(processing_threads[ibeam], stream, j);
        }
        // Start listening for packets.
        stream->start_stream();
        l1streams.push_back(stream);

        // Make RPC-serving object for each L1 node.
        string port = "tcp://127.0.0.1:" + to_string(rpc_port_l1_base + i);
        rpc_ports.push_back(port);

        //cout << "Starting RPC server on " << port << endl;
        shared_ptr<frb_rpc_server> rpc(new frb_rpc_server(stream));
        rpc->start(port);
        rpcs.push_back(rpc);

        usleep(10000);
    }

    for (int i=0; i<n_l0_nodes; i++) {
        // Create sim L0 nodes...

        // Currently, we don't have a fake L0 node object, so
        // just create streams for all L0 x L1 nodes

        cout << "L0 node " << i << ":" << endl;

        vector<shared_ptr<intensity_network_ostream> > nodestreams;
        for (int j=0; j<n_l1_nodes; j++) {

            ch_frb_io::intensity_network_ostream::initializer ini_params;
            for (int k=0; k<n_beams_per_l1_node; k++) {
                int ibeam = j*n_beams_per_l1_node + k;
                ini_params.beam_ids.push_back(ibeam);
            }
            for (int k=0; k<n_coarse_freq_per_l0; k++) {
                // not guaranteed to be contiguous... but are here.
                ini_params.coarse_freq_ids.push_back(i * n_coarse_freq_per_l0 + k);
            }
            ini_params.nupfreq = 16;
            ini_params.nfreq_coarse_per_packet = 4;
            ini_params.nt_per_packet = 16;
            ini_params.nt_per_chunk = 16;
            ini_params.fpga_counts_per_sample = 400;   // FIXME double-check this number

            ini_params.target_gbps = 0.1;
            
            int udp_port = udp_port_l1_base + j;
            ini_params.dstname = "127.0.0.1:" + to_string(udp_port);
            cout << "  Sending to L1 node " << j << " at "
                 << ini_params.dstname << endl;

            shared_ptr<intensity_network_ostream> ostream = 
                intensity_network_ostream::make(ini_params);
            nodestreams.push_back(ostream);
        }
        l0streams.push_back(nodestreams);
    }

    sleep(5);

    shared_ptr<intensity_network_ostream> ostream = l0streams[0][0];

    int npackets = nchunks * ostream->npackets_per_chunk;
    int nbytes = nchunks * ostream->nbytes_per_chunk;
    cout << "Packets per chunk: " << npackets << endl;
    cout << "Bytes per chunk: " << nbytes << endl;
    
    vector<float> intensity(ostream->elts_per_chunk, 0.0);
    vector<float> weights(ostream->elts_per_chunk, 1.0);
    int stride = ostream->nt_per_packet;

    // Send data.  The output stream object will automatically throttle packets to its target bandwidth.

    for (int ichunk = 0; ichunk < nchunks; ichunk++) {
        // To avoid the cost of simulating Gaussian noise, we use the following semi-arbitrary procedure.
        for (unsigned int i = 0; i < intensity.size(); i++)
            intensity[i] = ichunk + i;

        int64_t fpga_count = int64_t(ichunk) * int64_t(ostream->fpga_counts_per_chunk);

        cout << "Sending chunk " << ichunk << " (fpga_count " << fpga_count << ") L0/L1..." << endl;
        for (int i=0; i<n_l0_nodes; i++) {
            for (int j=0; j<n_l1_nodes; j++) {
                cout << i << "/" << j << " ";
                l0streams[i][j]->send_chunk(&intensity[0], &weights[0], stride, fpga_count);
            }
        }
        cout << endl;
    }

    sleep(10);

    /*
     cout << "L0 streams packets sent:" << endl;
     for (int i=0; i<n_l0_nodes; i++) {
     cout << "  L0 node " << i << ":  ";
     for (int j=0; j<n_l1_nodes; j++) {
     cout << l0streams[i][j]->npackets_sent << "  ";
     }
     cout << endl;
     }
     */


    // Now send some RPC requests to the L1 nodes.

    // zmq endpoint for making RPC requests
    zmq::context_t context(1);
    vector<shared_ptr<zmq::socket_t> > sockets;
    // Connect RPC sockets...
    for (int i=0; i<n_l1_nodes; i++) {
        shared_ptr<zmq::socket_t> socket(new zmq::socket_t(context, ZMQ_REQ));
        socket->connect(rpc_ports[i]);
        sockets.push_back(socket);
    }

    cout << "Sending requests to L1 nodes..." << endl;
    for (int i=0; i<n_l1_nodes; i++) {
        // RPC request buffer.
        msgpack::sbuffer buffer;
        string funcname = "get_beam_metadata";
        msgpack::pack(buffer, funcname);
        zmq::message_t request(buffer.data(), buffer.size(), NULL);
        cout << "Sending RPC request to L1 node " << i << endl;
        sockets[i]->send(request);

        usleep(100000);
    }

    usleep(100000);

    cout << "Receiving replies from L1 nodes..." << endl;
    for (int i=0; i<n_l1_nodes; i++) {
        //  Get the reply.
        usleep(100000);
        
        cout << "Receiving reply from L1 node " << i << endl;
        zmq::message_t reply;
        sockets[i]->recv(&reply);
        const char* reply_data = reinterpret_cast<const char *>(reply.data());
        msgpack::object_handle oh =
            msgpack::unpack(reply_data, reply.size());
        msgpack::object obj = oh.get();

        cout << "Reply: " << obj << endl;
        
    }

    return 0;
}
