#ifndef LIBBITCOIN_GETX_RESPONDER_HPP
#define LIBBITCOIN_GETX_RESPONDER_HPP

#include <system_error>

#include <bitcoin/types.hpp>
#include <bitcoin/async_service.hpp>
#include <bitcoin/messages.hpp>

namespace libbitcoin {

class getx_responder
{
public:
    getx_responder(async_service& service,
        blockchain& chain, transaction_pool& txpool);
    void monitor(channel_ptr node);

private:
    void receive_get_data(const std::error_code& ec,
        const get_data_type packet, channel_ptr node);

    void pool_tx(const std::error_code& ec, const transaction_type& tx,
        const hash_digest& tx_hash, channel_ptr node);
    void chain_tx(const std::error_code& ec,
        const transaction_type& tx, channel_ptr node);

    void send_block(const std::error_code& ec,
        const block_type blk, channel_ptr node);

    io_service& service_;
    blockchain& chain_;
    transaction_pool& txpool_;
};

} // libbitcoin

#endif

