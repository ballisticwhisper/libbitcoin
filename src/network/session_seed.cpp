/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * libbitcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/bitcoin/network/session_seed.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <bitcoin/bitcoin/error.hpp>
#include <bitcoin/bitcoin/config/authority.hpp>
#include <bitcoin/bitcoin/config/endpoint.hpp>
#include <bitcoin/bitcoin/message/network_address.hpp>
#include <bitcoin/bitcoin/network/connector.hpp>
#include <bitcoin/bitcoin/network/hosts.hpp>
#include <bitcoin/bitcoin/network/protocol_ping.hpp>
#include <bitcoin/bitcoin/network/protocol_seed.hpp>
#include <bitcoin/bitcoin/network/proxy.hpp>
#include <bitcoin/bitcoin/network/timeout.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>
#include <bitcoin/bitcoin/utility/logger.hpp>
#include <bitcoin/bitcoin/utility/string.hpp>
#include <bitcoin/bitcoin/utility/synchronizer.hpp>

INITIALIZE_TRACK(bc::network::session_seed);

namespace libbitcoin {
namespace network {

using std::placeholders::_1;
using std::placeholders::_2;

const config::endpoint::list session_seed::mainnet
{
    { "seed.bitnodes.io", 8333 },
    { "seed.bitcoinstats.com", 8333 },
    { "seed.bitcoin.sipa.be", 8333 },
    { "dnsseed.bluematt.me", 8333 },
    { "seed.bitcoin.jonasschnelli.ch", 8333 },
    { "dnsseed.bitcoin.dashjr.org", 8333 }
};

// Based on bitcoinstats.com/network/dns-servers
const config::endpoint::list session_seed::testnet
{
    { "testnet-seed.alexykot.me", 18333 },
    { "testnet-seed.bitcoin.petertodd.org", 18333 },
    { "testnet-seed.bluematt.me", 18333 },
    { "testnet-seed.bitcoin.schildbach.de", 18333 }
};

// This is not currently stoppable.
session_seed::session_seed(threadpool& pool, hosts& hosts, const timeout& timeouts,
    connector& network, const config::endpoint::list& seeds,
    const message::network_address& self)
  : dispatch_(pool),
    hosts_(hosts),
    timeouts_(timeouts),
    pool_(pool),
    network_(network),
    seeds_(seeds),
    self_(self),
    CONSTRUCT_TRACK(session_seed, LOG_NETWORK)
{
}

session_seed::~session_seed()
{
    log_info(LOG_PROTOCOL)
        << "Closed session_seed";
}

// TODO: notify all channels to stop.
// This will result in the completion handler being invoked.
// This is properly implemented through the planned session generalization.
void session_seed::start(handler complete)
{
    if (seeds_.empty() || hosts_.capacity() == 0)
    {
        log_info(LOG_PROTOCOL)
            << "No seeds and/or host capacity configured.";
        complete(error::operation_failed);
        return;
    }

    auto multiple =
        std::bind(&session_seed::handle_stopped,
            shared_from_this(), hosts_.size(), complete);

    // Require all seed callbacks before calling session_seed::handle_complete.
    auto single = synchronize(multiple, seeds_.size(), "session_seed", true);

    // Require one callback per channel before calling single.
    // We don't use parallel here because connect is itself asynchronous.
    for (const auto& seed: seeds_)
        start_connect(seed, synchronize(single, 1, seed.to_string()));
}

// This accepts no error code becuase individual seed errors are suppressed.
void session_seed::handle_stopped(size_t host_start_size, handler complete)
{
    // TODO: there is a race in that hosts_.size() is not ordered.
    // We succeed only if there is a seed count increase.
    if (hosts_.size() > host_start_size)
        complete(error::success);
    else
        complete(error::operation_failed);
}

void session_seed::start_connect(const config::endpoint& seed, handler complete)
{
    log_info(LOG_PROTOCOL)
        << "Contacting seed [" << seed << "]";

    // OUTBOUND CONNECT (concurrent)
    network_.connect(seed.host(), seed.port(),
        std::bind(&session_seed::handle_connected,
            shared_from_this(), _1, _2, seed, complete));
}

void session_seed::handle_connected(const code& ec, channel::ptr peer,
    const config::endpoint& seed, handler complete)
{
    if (ec)
    {
        log_info(LOG_PROTOCOL)
            << "Failure contacting seed [" << seed << "] "
            << ec.message();
        complete(ec);
        return;
    }

    log_info(LOG_PROTOCOL)
        << "Connected seed [" << seed << "] as " << peer->address();

    static const bool relay = false;
    const auto callback = 
        dispatch_.ordered_delegate(&session_seed::handle_handshake,
            shared_from_this(), _1, peer, seed, complete);

    // TODO: set height.
    const auto blockchain_height = 0;

    // Attach version protocol to the new connection (until complete).
    std::make_shared<protocol_version>(peer, pool_, timeouts_.handshake,
        callback, hosts_, self_, blockchain_height, relay)->start();

    // Protocols never start a channel.
    peer->start();
}

void session_seed::handle_handshake(const code& ec, channel::ptr peer,
    const config::endpoint& seed, handler complete)
{
    if (ec)
    {
        log_debug(LOG_PROTOCOL) << "Failure in seed handshake ["
            << peer->address() << "] " << ec.message();
        complete(ec);
        return;
    }

    // Attach ping protocol to the new connection (until peer stop event).
    std::make_shared<protocol_ping>(peer, pool_, timeouts_.heartbeat)->start();

    // Attach address seed protocol to the new connection.
    std::make_shared<protocol_seed>(peer, pool_, timeouts_.germination,
        complete, hosts_, self_)->start();
};

} // namespace network
} // namespace libbitcoin
