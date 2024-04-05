#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().buffer_size(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes();}

size_t TCPConnection::time_since_last_segment_received() const { return _ms_since_last_segment_received; }

size_t TCPConnection::write(const string &data) {
   //! \brief Write data to the outbound byte stream, and send it over TCP if possible
    //! \returns the number of bytes from `data` that were actually written.
    size_t ret = _sender.stream_in().write(data);

    // optional<WrappingInt32> ackno = _receiver.ackno();
    // size_t window_size = _receiver.window_size();
    // if (ackno.has_value())
    //     _sender._fill_window_(ackno.value(), window_size, true);
    // else
    //     _sender._fill_window_(WrappingInt32(0), window_size, false);
    _sender.fill_window();
    fill_queue();
    return ret;
}

void TCPConnection::segment_received(const TCPSegment &seg) { 
    _ms_since_last_segment_received = 0;
    if (seg.header().rst) {
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        //kill the connection permanently
        //这里要主动析构TCPConnect吗？
        return;
    }

    //将segment给予receiver处理，然后返回ackno和window_size大小
    //这里的ack_no和window_size是TCPSender要携带的，让对方的TCPReceiver知道的信息。
    //这里的接受到的segment可能情况：1.没有任何payload(这里payload指占用序号空间),只有ack和window_size的empty segment 
    //                            2.有payload的没有ack的non empty segment (First SYN)
    //                            3.有payload并且piggyback ack的non empty segment
    _receiver.segment_received(seg);
    optional<WrappingInt32> ackno = _receiver.ackno(); 
    if (!ackno.has_value())//这里的ackno可能没有值,说明未建立连接（未发送syn）就开始发送segment,这样的segment当作错误的segment不做任何处理
        return;
    // size_t window_size = _receiver.window_size();
    bool empty = true;
    if (seg.length_in_sequence_space() != 0)
        empty = false;


    //看哪边的发送方先发送FIN，先发送FIN的那一方需要linger after stream finish(就是需要需要在收到第四次挥手的ack后linger 2MSL） 
    //而先接收到FIN的那一方不需要linger，第三次挥手直接断开连接
    //_receiver.stream_out().input_ended()表示已经收到对方的FIN
    //!(_sender.stream_in().eof() && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2)表示自己的FIN还没有被对方确认
    //这里想的有点头疼，如果双方同时(或者相隔不久)发送FIN，双方自己的FIN都没得到确认？
    //那把哪一方认作先发送FIN的？
    if (_receiver.stream_out().input_ended() && !(_sender.stream_in().eof() && 
                _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2) ) {
        _linger_after_streams_finish = false;
    }


    if (seg.header().ack) { 
        if (empty) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
            fill_queue();
        } else { // if have ack and have payload, even if the window is full, here must send an ack
            bool flag = _sender.ack_received(seg.header().ackno, seg.header().win);
            if (!flag)
                _sender.send_empty_segment();
            fill_queue();
        }
    } else { // only first client syn don't have ack
        //有一种很搞的情况是自己connect先发送第一个SYN了，但是还没收到对方的确认。
        //对方也发送了一个没有ACK的SYN，这时三次握手变成了四次握手。这是自己应该给对方回一个对方SYN的ack，然后等待自己第一个SYN的ACK
        if (_sender.next_seqno_absolute() == 0) {
            _sender.fill_window();
            fill_queue();
        } else { //自己已经先发送第一个SYN了，你还给我发送一个没有ACK的SYN,那我只能给你回一个ack，然后等待你给我回我的第一个SYN的ack
            _sender.send_empty_segment();
            fill_queue();
        }

    }

}

bool TCPConnection::active() const { 
    if (_sender.stream_in().error() && _receiver.stream_out().error()) //rst set
        return false; 
    if (!_linger_after_streams_finish) { //说明自己不是首先发送FIN的一方,双方的流都结束后无需等待
        if (_receiver.stream_out().input_ended() && _sender.stream_in().eof() &&  _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 && _sender.bytes_in_flight() == 0)
            return false;
        else
            return true;
    } else { //自己是先发送FIN的一方，双方的流都结束后需等待2msl
        if (_receiver.stream_out().input_ended() && time_since_last_segment_received() >= 10 * _cfg.rt_timeout )
            return false;
        else
            return true;
    }
    
}



//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    _sender.tick(ms_since_last_tick);    
    fill_queue();
    _ms_since_last_segment_received += ms_since_last_tick;
    //重传次数超过限制，断开连接 unclean shutdown
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        //~TCPConnection();
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        TCPSegment seg{};
        seg.header().rst = 1;
        _sender.segments_out().push(seg);
        fill_queue();
    }
    // //先发送FIN的一方，在第四次挥手后等待时间超过2msl断开连接 clean shutdown
    // if (!active()) {
    //     //~TCPConnection();
    // }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    fill_queue();
}

void TCPConnection::connect() {
    //Initiate a connection by sending a SYN segment
    //这里是发送第一个SYN，第一个SYN是不携带ACK的
    _sender.fill_window();
    fill_queue();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            TCPSegment seg;
            seg.header().rst = 1;
            _sender.segments_out().push(seg);
            fill_queue();
            //_sender.send_empty_segment(WrappingInt32(0), 0, true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}


void TCPConnection::fill_queue() {
    optional<WrappingInt32> ackno = _receiver.ackno();   
    bool ack = false;
    if (ackno.has_value()) //除了发送方的第一个SYN外，其他的所有segment都有ack
        ack = true;
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (ack) {
            seg.header().ack = 1;
            seg.header().ackno = ackno.value();
        }    
        seg.header().win = _receiver.window_size();
        _segments_out.push(seg);
    }

}