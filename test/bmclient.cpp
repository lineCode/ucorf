#include <ucorf/client.h>
#include <ucorf/net_transport.h>
#include <ucorf/dispatcher.h>
#include <ucorf/server_finder.h>
#include "demo.rpc.h"
#include <iostream>
#include <cstdio>
#include <boost/smart_ptr/make_shared.hpp>
using std::cout;
using std::endl;
using namespace Echo;

static int concurrecy = 100;
static std::atomic<size_t> g_count{0};
static std::atomic<size_t> g_error{0};
static int g_all_time{0};
static int g_max_time{0};
boost_ec last_ec;

void show_status()
{
    static int c = 0;
    if (c++ % 10 == 0) {
        std::printf("------------------------- Co: %5d ------------------------------\n", concurrecy);
        std::printf("|   QPS   |  error  | average D | max D | Interval | last_error   \n");
    }

    static std::chrono::system_clock::time_point last_time = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    static size_t last_count = 0;
    static size_t last_error = 0;
    int qps = (int)(g_count - last_count);
    int err = (int)(g_error - last_error);
    std::string errinfo = last_ec.message();
    last_count = g_count;
    last_error = g_error;
    last_ec = boost_ec();

    std::printf("|%7d  |%7d  | %5d     |%5d  | %6d   | %s\n",
            qps, err, g_all_time / (qps + 1), g_max_time,
            (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count(),
            errinfo.c_str()
            );
    last_time = now;
    g_all_time = g_max_time = 0;
}

int main(int argc, char** argv)
{
    using namespace ucorf;

    if (argc > 1) {
        concurrecy = atoi(argv[1]);
    }

    FILE * lg = fopen("log", "a+");
    if (!lg) {
        cout << "open log error:" << strerror(errno) << endl;
        return 1;
    }

    co_sched.GetOptions().debug_output = lg;
    co_sched.GetOptions().debug = co::dbg_all;

    auto header_factory = [] {
        return boost::static_pointer_cast<IHeader>(boost::make_shared<UcorfHead>());
    };

    auto tp_factory = [] {
        return (ITransportClient*)new NetTransportClient;
    };

    ::network::OptionsUser tp_opt;
    tp_opt.max_pack_size_ = 40960;
    auto opt = boost::make_shared<Option>();
    opt->transport_opt = tp_opt;
    opt->rcv_timeout_ms = 0;

    std::unique_ptr<RobinDispatcher> dispatcher(new RobinDispatcher);
    std::unique_ptr<ServerFinder> finder(new ServerFinder);
    Client client;
    client.SetOption(opt)
        .SetTransportFactory(tp_factory)
        .SetHeaderFactory(header_factory)
        .SetServerFinder(std::move(finder))
        .SetDispatcher(std::move(dispatcher))
        .SetUrl("tcp://127.0.0.1:8080");
    UcorfEchoServiceStub stub(&client);

    for (int i = 0; i < concurrecy; ++i)
        go [&]{
            for (;;) {
                EchoRequest request;
                request.set_code(1);
                EchoResponse response;

                auto now = std::chrono::system_clock::now();
                boost_ec ec = stub.Echo(request, &response);
                if (ec) {
                    co_yield;
                    ++g_error;
                    last_ec = ec;
//                    cout << "rpc call error: " << ec.message() << endl;
                } else {
                    ++g_count;
                    int delay = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - now).count();
                    g_all_time += delay;
                    if (g_max_time < delay)
                        g_max_time = delay;
                }
            }
        };

    go []{
        for (;;) {
            show_status();
            co_sleep(1000);
        }
    };

    co_sched.RunLoop();

    return 0;
}
