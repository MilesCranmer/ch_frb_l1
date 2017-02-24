#include <unistd.h>
#include <string>
#include <iostream>

#include "ch_frb_io.hpp"
#include "assembled_chunk_msgpack.hpp"

#include "rf_pipelines.hpp"
#include "chime_packetizer.hpp"
#include "reverter.hpp"
#include "simpulse.hpp"

#include <l1-rpc.hpp>
#include <chlog.hpp>

using namespace std;
using namespace ch_frb_io;
using namespace rf_pipelines;
using namespace simpulse;

static void usage() {
    cout << "hdf5-stream [options] <HDF5 filenames ...>\n" <<
        //"    [-d DEST],  DEST like \"127.0.0.1:10252\"\n" <<
        //"    [-b BEAM],  BEAM an integer beam id\n" <<
        "    [-t Gbps],  throttle packet-sending rate\n" <<
        "    [-a <RPC address>]  like \"tcp://127.0.0.1:5555\"\n" <<
        "    [-p <RPC port number>] (integer port number)\n" <<
        endl;
}

/*
 rf_pipelines transforms:

 - make_chime_stream_from_filename_list (hdf5)

 - saver
 - inject frb (w/ S/N for beam 1)
 - (noise_adder?)
 - chime_packetizer --> sends to L1 (beam 1)
 - reverter

 ... repeat for each beam...
 - inject frb (w/ S/N for beam 2)
 - (noise_adder?)
 - chime_packetizer --> sends to L1 (beam 2)
 - reverter


 */

// this function is required to pull assembled_chunks from the end of
// the L1 pipeline.  These would go on to RFI removal and Bonsai
// dedispersion in real life L1...
static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> stream, int ithread);
                                   
int main(int argc, char **argv) {

    string dest = "127.0.0.1:10252";
    float gbps = 0.0;

    string rpc_port = "";
    int rpc_portnum = 0;
    
    int c;
    while ((c = getopt(argc, argv, "g:a:p:h")) != -1) {
        switch (c) {
            /*
             case 'd':
             dest = string(optarg);
             break;
             */
	case 'g':
	  gbps = atof(optarg);
	  break;

        case 'a':
            rpc_port = string(optarg);
            break;

        case 'p':
            rpc_portnum = atoi(optarg);
            break;

        case 'h':
        case '?':
        default:
	  usage();
	  return 0;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
      cout << "Need hdf5 input filenames!" << endl;
      usage();
      return -1;
    }

    vector<string> fns;
    for (int i=0; i<argc; i++)
      fns.push_back(string(argv[i]));

    auto stream = make_chime_stream_from_filename_list(fns);

    vector<shared_ptr<wi_transform> > transforms;

    int nt_per_chunk = ch_frb_io::constants::nt_per_assembled_chunk;
    int nfreq_coarse_per_packet = 4;
    int nt_per_packet = 2;
    float wt_cutoff = 1.;
    int beam = 1;
    
    shared_ptr<Saver> saver = make_saver(nt_per_chunk);
    shared_ptr<Reverter> rev = make_shared<Reverter>(saver);

    // HACK -- should get these from the stream...
    int nfreq = 16 * 1024;
    double freq_lo_mhz = 400;
    double freq_hi_mhz = 800;

    double dm = 500;
    double sm = 0.;
    double intrinsic_width = 0.010;
    double fluence = 1e5;

    double fluence_frac1 = 1.0;
    double fluence_frac2 = 0.1;
    double fluence_frac3 = 0.01;
    
    double spectral_index = 0.0;
    // the undispersed arrival time is ~ 3 seconds before the timestamp
    double undispersed_arrival_time = 711.3;

    vector<shared_ptr<single_pulse> > pulses1;
    pulses1.push_back(make_shared<single_pulse>
                      (1024, nfreq, freq_lo_mhz, freq_hi_mhz,
                       dm, sm, intrinsic_width, fluence * fluence_frac1,
                       spectral_index, undispersed_arrival_time));
    shared_ptr<wi_transform> pulser1 = make_pulse_adder(nt_per_chunk, pulses1);

    auto packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);

    transforms.push_back(saver);
    transforms.push_back(pulser1);
    transforms.push_back(packetizer);
    transforms.push_back(rev);

    beam = 2;
    packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);

    vector<shared_ptr<single_pulse> > pulses2;
    pulses2.push_back(make_shared<single_pulse>
                      (1024, nfreq, freq_lo_mhz, freq_hi_mhz,
                       dm, sm, intrinsic_width, fluence * fluence_frac2,
                       spectral_index, undispersed_arrival_time));
    shared_ptr<wi_transform> pulser2 = make_pulse_adder(nt_per_chunk, pulses2);
    
    // Can't just re-use the transform, must create new one.
    rev = make_shared<Reverter>(saver);

    transforms.push_back(pulser2);
    transforms.push_back(packetizer);
    transforms.push_back(rev);

    beam = 3;
    packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);

    vector<shared_ptr<single_pulse> > pulses3;
    pulses3.push_back(make_shared<single_pulse>
                      (1024, nfreq, freq_lo_mhz, freq_hi_mhz,
                       dm, sm, intrinsic_width, fluence * fluence_frac3,
                       spectral_index, undispersed_arrival_time));
    shared_ptr<wi_transform> pulser3 = make_pulse_adder(nt_per_chunk, pulses3);
    
    transforms.push_back(pulser3);
    transforms.push_back(packetizer);
    // don't need to restore the last one in the transform chain...
    //rev = make_shared<Reverter>(saver);
    //transforms.push_back(rev);

    //
    chime_log_open_socket();
    chime_log_set_thread_name("main");
    ch_frb_io::intensity_network_stream::initializer ini_params;
    ini_params.beam_ids = { 1, 2, 3 };
    ini_params.accept_end_of_stream_packets = false;
    ini_params.mandate_fast_kernels = false;
    ini_params.mandate_reference_kernels = true;

    // Make input stream object
    shared_ptr<ch_frb_io::intensity_network_stream> instream = ch_frb_io::intensity_network_stream::make(ini_params);

    // Spawn one processing thread per beam
    std::vector<std::thread> processing_threads;
    for (size_t ibeam = 0; ibeam < ini_params.beam_ids.size(); ibeam++)
        // Note: the processing thread gets 'ibeam', not the beam id,
        // because that is what get_assembled_chunk() takes
        processing_threads.push_back(std::thread(std::bind(processing_thread_main, instream, ibeam)));

    if ((rpc_port.length() == 0) && (rpc_portnum == 0))
        rpc_port = "tcp://127.0.0.1:5555";
    else if (rpc_portnum)
        rpc_port = "tcp://127.0.0.1:" + to_string(rpc_portnum);
    
    chlog("Starting RPC server on " << rpc_port);
    L1RpcServer rpc(instream, rpc_port);
    rpc.start();
    
    // Start listening for packets.
    instream->start_stream();


    // Start the rf_pipelines stream from hdf5 files to network
    stream->run(transforms);

    // This won't happen (we're ignoring end-of-stream packets)
    instream->join_threads();

    
}



static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                                    int ithread) {
    chime_log_set_thread_name("proc-" + std::to_string(ithread));
    chlog("Processing thread main: thread " << ithread);
    for (;;) {
	// Get assembled data from netwrok
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk)
	    break;  // End-of-stream reached
        chlog("Finished beam " << chunk->beam_id << ", chunk " << chunk->ichunk);
    }
}


