#pragma once

#include <libmuscle/mpp_message.hpp>
#include <libmuscle/mcp/transport_client.hpp>
#include <libmuscle/namespace.hpp>
#include <libmuscle/profiling.hpp>

#include <ymmsl/ymmsl.hpp>

#include <functional>
#include <string>
#include <tuple>
#include <vector>


namespace libmuscle { namespace _MUSCLE_IMPL_NS {


using ProfileData = std::tuple<
        ProfileTimestamp, ProfileTimestamp, ProfileTimestamp>;


class MockMPPClient {
    public:
        MockMPPClient(std::vector<std::string> const & locations);
        MockMPPClient(MockMPPClient const & rhs) = delete;
        MockMPPClient & operator=(MockMPPClient const & rhs) = delete;
        MockMPPClient(MockMPPClient && rhs) = delete;
        MockMPPClient & operator=(MockMPPClient && rhs) = delete;
        ~MockMPPClient();

        std::tuple<DataConstRef, ProfileData> receive(
                ::ymmsl::Reference const & receiver);

        void close();

        // Mock control variables
        static void reset();

        static int num_constructed;
        static MPPMessage next_receive_message;
        static ::ymmsl::Reference last_receiver;
        // Called after a mocked receive
        static std::function<void()> side_effect;

    private:
        static ::ymmsl::Settings make_overlay_();
};

using MPPClient = MockMPPClient;

} }

