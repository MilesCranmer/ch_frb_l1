// This is a toy program which simulates a packet stream at the full CHIME data rate,
// for timing purposes.  The receiver will usually be the counterpart toy program 'ch-frb-l1'.
//
// It can be pointed at any IP address, but we usually just run over the loopback IP 127.0.0.1.
//
// A major caveat is that the intensity values it sends are just arbitrary numbers, not
// something more interesting like Gaussian random noise, or even more interesting such
// as noise + simulated FRB's.  This is because the current simulation code is too slow
// to simulate anything interesting (even Gaussian noise) at the full CHIME data rate,
// and fixing this is a to-do item (see discussion in ch_frb_io/README).

#include <random>
#include <iostream>
#include <cstring>
#include <ch_frb_io.hpp>

using namespace std;
using ch_frb_io::lexical_cast;

static const double default_gb_to_simulate = 10.0;

static void usage(const char *extra=nullptr)
{
    cerr << "ch-frb-simulate-l0 [-t target_gbps] [-G gb_to_simulate] HOST[:PORT]\n"
	 << "   if -G is unspecified, then gb_to_simulate will default to " << default_gb_to_simulate << "\n"
	 << "   if -t is unspecified, then target_gbps will default to " << ch_frb_io::constants::default_gbps << "\n"
	 << "   if -t 0.0 is specified, then data will be sent as quickly as possible\n"
	 << "   if PORT is unspecified, it will default to " << ch_frb_io::constants::default_udp_port << "\n";

    if (extra)
	cerr << extra << endl;

    exit(2);
}

// misc helper
static vector<int> vrange(int n)
{
    vector<int> ret(n);
    for (int i = 0; i < n; i++)
	ret[i] = i;
    return ret;
}


float cpu_time() {
    struct rusage r;
    float sofar;
    if (getrusage(RUSAGE_SELF, &r)) {
        cerr << "Failed to get resource usage" << endl;
        return -1.0;
    }
    sofar = (float)(r.ru_utime.tv_sec + r.ru_stime.tv_sec) +
        (1e-6 * (r.ru_utime.tv_usec + r.ru_stime.tv_usec));
    return sofar;
}

class Gaussian {
public:
    Gaussian(double mean=0.0, double stddev=1.0) :
        _n_uni(0),
        _uni(-1.0, 1.0), _mean(mean), _stddev(stddev),
        _have_y2(false)
    {}

    template< class Generator >
    double operator()( Generator& g ) {
        if (_have_y2) {
            _have_y2 = false;
            return _mean + _y2 * _stddev;
        }
        double x1, x2, w;
        do {
            x1 = _uni(g);
            x2 = _uni(g);
            _n_uni += 2;
            w = x1 * x1 + x2 * x2;
        } while ( w >= 1.0 );

        w = sqrt( (-2.0 * log(w)) / w );
        _y2 = x2 * w;
        _have_y2 = true;
        return _mean + x1 * w * _stddev;
    }

    int _n_uni;

protected:
    std::uniform_real_distribution<> _uni;
    double _mean;
    double _stddev;

    bool _have_y2;
    double _y2;
};

int main(int argc, char **argv)
{
    double gb_to_simulate = default_gb_to_simulate;

    ch_frb_io::intensity_network_ostream::initializer ini_params;
    ini_params.beam_ids = vrange(8);
    ini_params.coarse_freq_ids = vrange(ch_frb_io::constants::nfreq_coarse_tot);
    ini_params.nupfreq = 16;
    ini_params.nfreq_coarse_per_packet = 4;
    ini_params.nt_per_packet = 16;
    ini_params.nt_per_chunk = 16;
    ini_params.fpga_counts_per_sample = 400;   // FIXME double-check this number

    // Low-budget command line parsing

    int iarg = 1;

    while (iarg < argc) {
	if (!strcmp(argv[iarg], "-t")) {
	    if (iarg >= argc-1)
		usage();
	    if (!lexical_cast(argv[iarg+1], ini_params.target_gbps))
		usage();
	    iarg += 2;
	}
	else if (!strcmp(argv[iarg], "-G")) {
	    if (iarg >= argc-1)
		usage();
	    if (!lexical_cast(argv[iarg+1], gb_to_simulate))
		usage();
	    if (gb_to_simulate <= 0.0)
		usage();
	    iarg += 2;
	}	    
	else {
	    if (ini_params.dstname.size() > 0)
		usage();
	    ini_params.dstname = argv[iarg];
	    iarg++;
	}
    }

    if (ini_params.dstname.size() == 0)
	usage();

    // Make output stream object and print a little summary info

    auto ostream = ch_frb_io::intensity_network_ostream::make(ini_params);

    int nchunks = int(gb_to_simulate * 1.0e9 / ostream->nbytes_per_chunk) + 1;
    int npackets = nchunks * ostream->npackets_per_chunk;
    int nbytes = nchunks * ostream->nbytes_per_chunk;
    cerr << "ch-frb-simulate-l0: sending " << (nbytes/1.0e9) << " GB data (" << nchunks << " chunks, " << npackets << " packets)\n";

    vector<float> intensity(ostream->elts_per_chunk, 0.0);
    vector<float> weights(ostream->elts_per_chunk, 1.0);
    int stride = ostream->nt_per_packet;

    // I'd like to simulate Gaussian noise, but the Gaussian random number generation 
    // actually turns out to be a bottleneck!
    std::random_device rd;
    unsigned int seed = rd();
    std::mt19937 rng(seed);

    std::mt19937_64 rng64(seed);

    int N = 100000000;
    
    float t0;
    uint_fast32_t sum;

    t0 = cpu_time();
    sum = 0;
    for (int i=0; i<N; i++) {
        sum += i;
    }
    cout << "CPU time: just sum: " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    t0 = cpu_time();
    sum = 0;
    for (int i=0; i<N; i++) {
        sum += rng();
    }
    cout << "CPU time: mt.rng(): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    t0 = cpu_time();
    sum = 0;
    for (int i=0; i<N; i++) {
        sum += rng64();
    }
    cout << "CPU time: mt.rng64(): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    std::minstd_rand0 rand0(seed);
    t0 = cpu_time();
    sum = 0;
    for (int i=0; i<N; i++) {
        sum += rand0();
    }
    cout << "CPU time: minstd_0(): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    std::minstd_rand rand1(seed);
    t0 = cpu_time();
    sum = 0;
    for (int i=0; i<N; i++) {
        sum += rand1();
    }
    cout << "CPU time: minstd(): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    std::ranlux24_base ranlux(seed);
    t0 = cpu_time();
    sum = 0;
    for (int i=0; i<N; i++) {
        sum += ranlux();
    }
    cout << "CPU time: ranlux(): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    std::normal_distribution<> dist;
    t0 = cpu_time();
    float fsum = 0.0;
    for (int i=0; i<N; i++) {
        fsum += dist(rng);
    }
    cout << "CPU time: normal(rng): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    Gaussian g;
    g._n_uni = 0;
    t0 = cpu_time();
    fsum = 0.0;
    for (int i=0; i<N; i++) {
        fsum += g(rng);
    }
    cout << "CPU time: Gaussian(rng): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;
    cout << "N uniform rng calls: " << g._n_uni << endl;

    t0 = cpu_time();
    fsum = 0.0;
    g._n_uni = 0;
    for (int i=0; i<N; i++) {
        fsum += g(rand1);
    }
    cout << "CPU time: Gaussian(rand_1): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;
    cout << "N uniform rng calls: " << g._n_uni << endl;

    t0 = cpu_time();
    fsum = 0.0;
    for (int i=0; i<N; i++) {
        fsum += dist(rand1);
    }
    cout << "CPU time: normal(rand_1): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    std::uniform_real_distribution<> udist;
    t0 = cpu_time();
    fsum = 0.0;
    for (int i=0; i<N; i++) {
        fsum += udist(rng);
    }
    cout << "CPU time: uniform(rng): " << (cpu_time() - t0) << endl;
    cout << "  sum " << sum << endl;

    return 0;

    // Send data.  The output stream object will automatically throttle packets to its target bandwidth.

    for (int ichunk = 0; ichunk < nchunks; ichunk++) {
	// To avoid the cost of simulating Gaussian noise, we use the following semi-arbitrary procedure.
	for (unsigned int i = 0; i < intensity.size(); i++)
	    intensity[i] = ichunk + i;

	int64_t fpga_count = int64_t(ichunk) * int64_t(ostream->fpga_counts_per_chunk);
	ostream->send_chunk(&intensity[0], &weights[0], stride, fpga_count);
    }


    // All done!

    ostream->end_stream(true);  // "true" joins network thread

    return 0;
}
