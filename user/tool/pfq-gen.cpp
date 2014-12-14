/***************************************************************
 *
 * (C) 2011 - Nicola Bonelli <nicola@pfq.io>
 *            Andrea Di Pietro <andrea.dipietro@for.unipi.it>
 *
 ****************************************************************/

#include <iostream>
#include <fstream>
#include <sstream>

#include <thread>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iterator>
#include <atomic>
#include <cmath>
#include <tuple>
#include <unordered_set>
#include <random>

#include <binding.hpp>
#include <affinity.hpp>

#include <pfq/pfq.hpp>
#include <pfq/util.hpp>

#include <linux/ip.h>
#include <linux/udp.h>

#include <vt100.hpp>


using namespace pfq;


char *make_packet(size_t n)
{
    static unsigned char ping[98] =
    {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xbf, /* L`..UF.. */
        0x97, 0xe2, 0xff, 0xae, 0x08, 0x00, 0x45, 0x00, /* ......E. */
        0x00, 0x54, 0xb3, 0xf9, 0x40, 0x00, 0x40, 0x01, /* .T..@.@. */
        0xf5, 0x32, 0xc0, 0xa8, 0x00, 0x02, 0xad, 0xc2, /* .2...... */
        0x23, 0x10, 0x08, 0x00, 0xf2, 0xea, 0x42, 0x04, /* #.....B. */
        0x00, 0x01, 0xfe, 0xeb, 0xfc, 0x52, 0x00, 0x00, /* .....R.. */
        0x00, 0x00, 0x06, 0xfe, 0x02, 0x00, 0x00, 0x00, /* ........ */
        0x00, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, /* ........ */
        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, /* ........ */
        0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, /* .. !"#$% */
        0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, /* &'()*+,- */
        0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, /* ./012345 */
        0x36, 0x37                                      /* 67 */
    };

    auto p = new char[n];

    memcpy(p, ping, std::min(n, sizeof(ping)));

    for(auto i = sizeof(ping); i < n; i++)
    {
        p[i] = static_cast<char>(0x38 + i - sizeof(ping));
    }

    return p;
}


namespace opt
{
    size_t flush   = 1;
    size_t len     = 64;
    size_t slots   = 4096;
    bool   async   = false;
    bool   rand_ip = false;
    char *packet   = nullptr;
}


namespace thread
{
    struct context
    {
        context(int id, const binding &b, int kcpu)
        : m_id(id)
        , m_kcpu(kcpu)
        , m_bind(b)
        , m_pfq()
        , m_sent(std::unique_ptr<std::atomic_ullong>(new std::atomic_ullong(0)))
        , m_fail(std::unique_ptr<std::atomic_ullong>(new std::atomic_ullong(0)))
        , m_gen()
        {
            if (m_bind.dev.empty())
                throw std::runtime_error("context: device unspecified");

            if (m_bind.queue.empty())
                m_bind.queue.push_back(-1);

            auto q = pfq::socket(param::list, param::maxlen{opt::len},
                                              param::tx_slots{opt::slots});

            std::cout << "thread     : " << id << " -> "  << show_binding(m_bind) << std::endl;

            if (m_bind.queue.size())
            {
                for(unsigned int n = 0; n < m_bind.queue.size(); n++)
                {
                    q.bind_tx (m_bind.dev.at(0).c_str(), m_bind.queue[n], opt::async ? (n + m_kcpu) : -1);
                }
            }
            else
            {
                q.bind_tx (m_bind.dev.at(0).c_str(), any_queue, opt::async ? m_kcpu : -1);
            }

            q.enable();
            m_pfq = std::move(q);
        }

        context(const context &) = delete;
        context& operator=(const context &) = delete;

        context(context &&) = default;
        context& operator=(context &&) = default;

        void operator()()
        {
            auto ip = reinterpret_cast<iphdr *>(opt::packet + 14);

            for(;;)
            {
                if (opt::rand_ip)
                {
                    ip->saddr = static_cast<uint32_t>(m_gen());
                    ip->daddr = static_cast<uint32_t>(m_gen());
                }

                if (m_pfq.send_async(pfq::const_buffer(reinterpret_cast<const char *>(opt::packet), opt::len), opt::flush))
                    m_sent->fetch_add(1, std::memory_order_relaxed);
                else
                    m_fail->fetch_add(1, std::memory_order_relaxed);
            }
        }

        std::tuple<pfq_stats, uint64_t, uint64_t>
        stats() const
        {
            pfq_stats ret = {0,0,0,0,0,0,0};

            ret += m_pfq.stats();

            return std::make_tuple(ret, m_sent->load(std::memory_order_relaxed),
                                        m_fail->load(std::memory_order_relaxed));
        }

    private:
        int m_id;
        int m_kcpu;

        binding m_bind;

        pfq::socket m_pfq;

        std::unique_ptr<std::atomic_ullong> m_sent;
        std::unique_ptr<std::atomic_ullong> m_fail;

        std::mt19937 m_gen;
    };

}


bool any_strcmp(const char *arg, const char *opt)
{
    return strcmp(arg,opt) == 0;
}
template <typename ...Ts>
bool any_strcmp(const char *arg, const char *opt, Ts&&...args)
{
    return (strcmp(arg,opt) == 0 ? true : any_strcmp(arg, std::forward<Ts>(args)...));
}


template <typename U, typename T, typename Duration>
U persecond(T value, Duration dur)
{
    return static_cast<U>(value) * 1000000 /
        std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
}

template <typename T>
std::string to_string_(std::ostringstream &out, T &&arg)
{
    out << std::move(arg);
    return out.str();
}
template <typename T, typename ...Ts>
std::string to_string_(std::ostringstream &out, T &&arg, Ts&&... args)
{
    out << std::move(arg);
    return to_string_(out, std::forward<Ts>(args)...);
}
template <typename ...Ts>
inline std::string
to_string(Ts&& ... args)
{
    std::ostringstream out;
    return to_string_(out, std::forward<Ts>(args)...);
}

template <typename T>
std::string
pretty(T value)
{
    if (value < 1000000000) {
    if (value < 1000000) {
    if (value < 1000) {
         return to_string(value);
    }
    else return to_string(value/1000, "_K");
    }
    else return to_string(value/1000000, "_M");
    }
    else return to_string(value/1000000000, "_G");
}


void usage(std::string name)
{
    throw std::runtime_error
    (
        "usage: " + std::move(name) + " [OPTIONS]\n\n"
        " -h --help                     Display this help\n"
        " -l --len INT                  Set packet lenght\n"
        " -s --queue-slots INT          Set tx queue lenght\n"
        " -a --async                    Async with kernel threads\n"
        " -R --rand-ip                  Randomize IP addresses\n"
        " -f --flush INT                Set flush len, used in async tx\n"
        " -t --thread BINDING\n\n"
        "      BINDING = " + pfq::binding_format
    );
}


std::vector<thread::context *> thread_ctx;

int
main(int argc, char *argv[])
try
{
    if (argc < 2)
        usage(argv[0]);

    std::vector<pfq::binding> binding;

    for(int i = 1; i < argc; ++i)
    {
        if ( any_strcmp(argv[i], "-f", "--flush") )
        {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("hint missing");
            }

            opt::flush = static_cast<size_t>(std::atoi(argv[i]));
            continue;
        }

        if ( any_strcmp(argv[i], "-l", "--len") )
        {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("length missing");
            }

            opt::len = static_cast<size_t>(std::atoi(argv[i]));
            continue;
        }

        if ( any_strcmp(argv[i], "-s", "--slots") )
        {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("slots missing");
            }

            opt::slots = static_cast<size_t>(std::atoi(argv[i]));
            continue;
        }

        if ( any_strcmp(argv[i], "-a", "--async") )
        {
            opt::async = true;
            continue;
        }

        if ( any_strcmp(argv[i], "-R", "--rand-ip") )
        {
            opt::rand_ip = true;
            continue;
        }

        if ( any_strcmp(argv[i], "-t", "--thread") )
        {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("binding missing");
            }

            binding.push_back(make_binding(argv[i]));
            continue;
        }

        if ( any_strcmp(argv[i], "-?", "-h", "--help") )
            usage(argv[0]);

        throw std::runtime_error(std::string("pfq-gen: ") + argv[i] + " unknown option");
    }


    std::cout << "async      : "  << std::boolalpha << opt::async << std::endl;
    std::cout << "rand_ip    : "  << std::boolalpha << opt::rand_ip << std::endl;
    std::cout << "len        : "  << opt::len << std::endl;

    if (!opt::async)
        std::cout << "flush-hint : "  << opt::flush << std::endl;

    if (opt::slots == 0)
        throw std::runtime_error("tx_slots set to 0!");

    opt::packet = make_packet(opt::len);

    // process binding:
    //

    for(size_t i = 0; i < binding.size(); ++i)
    {
        if (binding[i].queue.empty())
        {
            auto num_queues = get_num_queues(binding[i].dev.at(0).c_str());

            std::cout << "device     : " << binding[i].dev.at(0) << ", " << num_queues << " queues detected." << std::endl;

            for(size_t n = 0; n < num_queues; ++n)
            {
                binding[i].queue.push_back(n);
            }
        }
    }

    auto mq = std::any_of(std::begin(binding), std::end(binding),
                [](pfq::binding const &b) { return b.queue.size() > 1; });

    if (!opt::rand_ip && mq)
    {
        std::cout << vt100::BOLD << "*** Multiple queue detected! Consider to randomize IP addresses with -R option! ***" << vt100::RESET << std::endl;
    }

    if (!opt::async && mq)
    {
        std::cout << vt100::BOLD << "*** Multiple queue detected! Consider to enable asynchronous transmission, with -a option! ***" << vt100::RESET << std::endl;
    }

    // create thread context:
    //

    int kcore = 0;

    for(unsigned int i = 0; i < binding.size(); ++i)
    {
        thread_ctx.push_back(new thread::context(static_cast<int>(i), binding[i], kcore));
        kcore += std::max<size_t>(binding[i].queue.size(), 1);
    }

    // create threads:

    size_t i = 0;
    std::for_each(binding.begin(), binding.end(), [&](pfq::binding const &b) {

        auto t = new std::thread(std::ref(*thread_ctx[i++]));

        extra::set_affinity(*t, b.core);

        t->detach();

    });

    pfq_stats cur, prec = {0,0,0,0,0,0,0};
    uint64_t sent, sent_ = 0;
    uint64_t fail, fail_ = 0;

    std::cout << "------------ gen started ------------\n";

    auto begin = std::chrono::system_clock::now();

    for(int y=0; true; y++)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        cur = {0,0,0,0,0,0,0};
        sent = 0;
        fail = 0;

        std::for_each(thread_ctx.begin(), thread_ctx.end(), [&](const thread::context *c)
        {
            auto p = c->stats();

            cur  += std::get<0>(p);
            sent += std::get<1>(p);
            fail += std::get<2>(p);
        });

        auto end   = std::chrono::system_clock::now();
        auto delta = end-begin;

        std::cout << "stats: { " << cur << " } -> user: { "
                  << vt100::BOLD << persecond<int64_t>(sent - sent_, delta) << ' ' << persecond<int64_t>(fail-fail_, delta) << vt100::RESET << " } - "
                  << "sent: " << vt100::BOLD << persecond<int64_t>(cur.sent - prec.sent, delta) << vt100::RESET << " pkt/sec - "
                  << "band: " << vt100::BOLD << pretty(persecond<double>((cur.sent - prec.sent) * opt::len * 8, delta)) << vt100::RESET << "bit/sec - "
                  << "disc: " << vt100::BOLD << persecond<int64_t>(cur.disc - prec.disc, delta) << vt100::RESET << " pkt/sec" << std::endl;

        prec = cur, begin = end;
        sent_ = sent;
        fail_ = fail;
    }

}
catch(std::exception &e)
{
    std::cerr << e.what() << std::endl;
}
