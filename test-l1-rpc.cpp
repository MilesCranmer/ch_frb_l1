#include <unistd.h>
#include <iostream>
#include <sstream>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>
#include <chlog.hpp>

using namespace std;
using namespace ch_frb_io;

#include "CivetServer.h"

class ExampleHandler : public CivetHandler {
public:
    bool
    handleGet(CivetServer *server, struct mg_connection *conn)
    {
        cout << "handle GET" << endl;
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n\r\n");
        mg_printf(conn, "Hello world.\r\n");
        return true;
    }
};

shared_ptr<CivetServer> start_web_server(int port) {
    //"document_root", DOCUMENT_ROOT, "listening_ports", PORT, 0};
    std::vector<std::string> options;
    options.push_back("listening_ports");
    options.push_back(to_string(port));
    shared_ptr<CivetServer> server = make_shared<CivetServer>(options);
    // we're going to memory-leak this guy.
    ExampleHandler* h_ex = new ExampleHandler();
    server->addHandler("/metrics", h_ex);
    return server;
}

int main(int argc, char** argv) {
    int beam = 77;
    string port = "";
    int portnum = 0;
    int udpport = 0;
    int wait = 0;
    float chunksleep = 0;
    int nchunks = 100;

    int c;
    while ((c = getopt(argc, argv, "a:p:b:u:ws:n:h")) != -1) {
        switch (c) {
        case 'a':
            port = string(optarg);
            break;

        case 'p':
            portnum = atoi(optarg);
            break;

        case 'b':
            beam = atoi(optarg);
            break;

        case 'u':
            udpport = atoi(optarg);
            break;

        case 'w':
            wait = 1;
            break;

        case 's':
            chunksleep = atof(optarg);
            break;

        case 'n':
            nchunks = atoi(optarg);
            break;

        case 'h':
        case '?':
        default:
            cout << string(argv[0]) << ": [-a <address>] [-p <port number>] [-b <beam id>] [-u <L1 udp-port>] [-w to wait indef] [-s <sleep between chunks>] [-n <N chunks>] [-h for help]" << endl;
            cout << "eg,  -a tcp://127.0.0.1:5555" << endl;
            cout << "     -p 5555" << endl;
            cout << "     -b 78" << endl;
            return 0;
        }
    }
    argc -= optind;
    argv += optind;

    chime_log_open_socket();
    chime_log_set_thread_name("main");

    int web_port = 8081;
    shared_ptr<CivetServer> webserver = start_web_server(web_port);
    cout << "Started web server on port " << web_port << endl;
    
    int nupfreq = 4;
    int nt_per = 16;
    int fpga_per = 400;

    intensity_network_stream::initializer ini;
    ini.beam_ids.push_back(beam);
    ini.nupfreq = nupfreq;
    ini.nt_per_packet = nt_per;
    ini.fpga_counts_per_sample = fpga_per;
    //ini.force_fast_kernels = HAVE_AVX2;

    if (udpport)
        ini.udp_port = udpport;

    output_device::initializer out_params;
    out_params.device_name = "";
    // debug
    out_params.verbosity = 3;
    std::shared_ptr<output_device> outdev = output_device::make(out_params);

    ini.output_devices.push_back(outdev);

    shared_ptr<intensity_network_stream> stream = intensity_network_stream::make(ini);
    stream->start_stream();

    // listen on localhost only, for local-machine testing (trying to
    // listen on other ports triggers GUI window popup warnings on Mac
    // OSX)
    if ((port.length() == 0) && (portnum == 0))
        port = "tcp://127.0.0.1:5555";
    else if (portnum)
        port = "tcp://127.0.0.1:" + to_string(portnum);

    chlog("Starting RPC server on port " << port);
    L1RpcServer rpc(stream, port);
    std::thread rpc_thread = rpc.start();

    std::random_device rd;
    std::mt19937 rng(rd());
    rng.seed(42);
    std::uniform_int_distribution<> rando(0,1);

    std::vector<int> consumed_chunks;

    int backlog = 0;
    int failed_push = 0;

    for (int i=0; i<nchunks; i++) {
	assembled_chunk::initializer ini_params;
	ini_params.beam_id = beam;
	ini_params.nupfreq = nupfreq;
	ini_params.nt_per_packet = nt_per;
	ini_params.fpga_counts_per_sample = fpga_per;
	ini_params.ichunk = i;

	unique_ptr<assembled_chunk> uch = assembled_chunk::make(ini_params);
        assembled_chunk* ch = uch.release();
        chlog("Injecting " << i);
        if (stream->inject_assembled_chunk(ch))
            chlog("Injected " << i);
        else {
            chlog("Inject failed (ring buffer full)");
            failed_push++;
        }

        // downstream thread consumes with a lag of 2...
        if (i >= 2) {
            // Randomly consume 0 to 2 chunks
            shared_ptr<assembled_chunk> ach;
            if ((backlog > 0) && rando(rng)) {
                cout << "Downstream consumes a chunk (backlog)" << endl;
                ach = stream->get_assembled_chunk(0, false);
                if (ach) {
                    cout << "  (chunk " << ach->ichunk << ")" << endl;
                    consumed_chunks.push_back(ach->ichunk);
                    backlog--;
                }
            }
            if (rando(rng)) {
                chlog("Downstream consumes a chunk");
                ach = stream->get_assembled_chunk(0, false);
                if (ach) {
                    chlog("  (chunk " << ach->ichunk << ")");
                    consumed_chunks.push_back(ach->ichunk);
                } else
                    backlog++;
            }
            if (rando(rng)) {
                chlog("Downstream consumes a chunk");
                ach = stream->get_assembled_chunk(0, false);
                if (ach) {
                    chlog("  (chunk " << ach->ichunk << ")");
                    consumed_chunks.push_back(ach->ichunk);
                } else
                    backlog++;
            }
        }

        cout << endl;
        stream->print_state();
        cout << endl;


        if (chunksleep)
            usleep(int(chunksleep * 1000000));

    }

    //cout << "End state:" << endl;
    //rb->print();
    //cout << endl;

    vector<vector<pair<shared_ptr<assembled_chunk>, uint64_t> > > chunks;
    chlog("Test retrieving chunks...");
    //rb->retrieve(30000000, 50000000, chunks);
    vector<uint64_t> beams;
    beams.push_back(beam);
    chunks = stream->get_ringbuf_snapshots(beams);
    stringstream ss;
    ss << "Got " << chunks.size() << " beams, with number of chunks:";
    for (auto it = chunks.begin(); it != chunks.end(); it++) {
        ss << " " << it->size();
    }
    string s = ss.str();
    chlog(s);


    int Nchunk = 1024 * 400;
    for (auto it = chunks.begin(); it != chunks.end(); it++) {
        cout << "[" << endl;
        for (auto it2 = it->begin(); it2 != it->end(); it2++) {
            shared_ptr<assembled_chunk> ch = it2->first;
            cout << "  chunk " << (ch->fpga_begin / Nchunk) << " to " <<
                (ch->fpga_end / Nchunk) << ", N chunks " <<
                ((ch->fpga_end - ch->fpga_begin) / Nchunk) << endl;
        }
        cout << "]" << endl;
    }

    cout << "State:" << endl;
    stream->print_state();


    for (int nwait=0;; nwait++) {
        if (rpc.is_shutdown())
            break;
        usleep(1000000);
        if (!wait && nwait >= 30)
            break;
    }
    chlog("Exiting");

    rpc.do_shutdown();
    rpc_thread.join();
}


/*
int main() {
    cout << "Creating ringbuf..." << endl;
    Ringbuf<int> rb(4);

    int a = 42;
    int b = 43;
    int c = 44;

    cout << "Pushing" << endl;
    rb.push(&a);
    cout << "Pushing" << endl;
    rb.push(&b);
    cout << "Pushing" << endl;
    rb.push(&c);

    cout << "Popping" << endl;
    shared_ptr<int> p1 = rb.pop();
    cout << "Popping" << endl;
    shared_ptr<int> p2 = rb.pop();
    cout << "Dropping" << endl;
    p1.reset();
    cout << endl;

    int d = 45;
    int e = 46;
    int f = 47;
    int g = 48;

    cout << "Pushing d..." << endl;
    shared_ptr<int> pd = rb.push(&d);

    cout << endl;
    cout << "Pushing e..." << endl;
    shared_ptr<int> pe = rb.push(&e);

    cout << endl;
    cout << "Pushing f..." << endl;
    shared_ptr<int> pf = rb.push(&f);

    cout << endl;
    cout << "Pushing g..." << endl;
    rb.push(&g);

    cout << "Done" << endl;

}
 */

