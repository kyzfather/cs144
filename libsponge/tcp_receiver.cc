#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//感觉携带data的FIN的segment，和不携带data的FIN的segment，回复的ACK不一样啊？？？？这个怎么区分
//一样的，即使是携带data和fin的segment，假如data大小为n1, 这个segment的seqno为n0, ackno也是n0+n1+1
//FIN固定要占一个sequence num
//(同样SYN也是固定占一个sequence num的，比如第一个握手SYN，初始序号为n0, data大小为2，那么ackno为n0+3)
optional<WrappingInt32> TCPReceiver::ackno() const { 
    optional<WrappingInt32> ret;
    if (!_isn.has_value()) //当前的_isn没有初始化，说明没有收到SYN建立连接的数据包
        return ret; 
    else {
        uint64_t index = _reassembler.getIndex(); //stream index;
        uint64_t absolute_seqno = index + 1; //absolute_seqno
        
        if (index == 0 && _reassembler.stream_out().input_ended()) { //special case: SYN=1 AND FIN=1
            return wrap(2, _isn.value());
        }

        if (!_reassembler.stream_out().input_ended()) { //对非FIN的回复ack
            ret = wrap(absolute_seqno, _isn.value());
            return ret;
        } else { //对FIN的回复ack
            ret = wrap(absolute_seqno + 1, _isn.value());
            return ret;
        }
    }
}

/*
  有些类似于选择ack，如果收到乱序的segment，会先将后面的数据包先缓存起来，而不是直接丢弃(StreamAssembler里的处理)
  然后receiver会向发送端发送中间缺失的数据包的ack，然后如果后续收到这部分中间缺失的数据包，会回复累积ACK
  这种实现方式显然不是go-back-N
  如果receiver收到的segment中有FIN标记，利用StreamAssembler的push_substring()时候要设置eof标记
  但是这个带有FIN标记的segment之前可能还有没有收到的数据包，那么暂时receiver的ByteStream暂时还不是input_ended()的状态

  小心报头中SYN和FIN同时为1的情况。  note:first syn can carry data
*/
void TCPReceiver::segment_received(const TCPSegment &seg) {
    //先按自己的想法写，可能理解的有问题，但先自己想着写然后对照参考代码看哪些理解不对。
    //收到segment先判断是否有SYN标记，如果有说明是握手阶段，记录起始的seq no
    //TCP三次握手前两次握手不能携带数据
    TCPHeader head = seg.header();
    Buffer payload = seg.payload();


    //A byte with invalid stream index should be ignored
    //special test case: first segment(syn = 1, data='') second segment(syn = 1 data='a'): second segment's data'a' cant be received
    if(ackno().has_value() && head.seqno == _isn.value() && payload.str().size() != 0)
        return;

    //listen阶段 （第一次握手前）
    //note:first syn can carry data
    if (!ackno().has_value()) { //第一次握手的syn为1  第三次握手syn为0 且第一次握手不会携带数据
        if (head.syn == true)
            _isn = head.seqno;
        //return; first syn can carry data, can't return here
    }

    //握手完成，receive阶段 （第一次握手后~收到第一次挥手）
    //receive阶段可能收到第一次挥手的FIN数据包
    //处理数据，获取头部的seqno，然后转换为absolute seqno，stream index = absolute seqno - 1 然后调用StreamReassembler往里面写
    //注意，挥手阶段不可以再发送数据
    if (ackno().has_value() && !_reassembler.stream_out().input_ended()) { 
        // if (head.fin == true) {
        //     _reassembler.stream_out().end_input();
        //     return;
        // } 
        bool eof = false;
        if (head.fin == true)
            eof = true;
        WrappingInt32 seqno = head.seqno;


        //checkpoint含义：In your TCP implementation, you’ll use the index of the last reassembled byte as the checkpoint.
        uint64_t checkpoint = _reassembler.getIndex(); //StreamReassembler中记录着第一个没有重组的stream index下标
        uint64_t absolute_index = unwrap(seqno, _isn.value(), checkpoint);
        uint64_t stream_index ;
        if (absolute_index == 0) { //SYN=1 AND FIN=1
            stream_index = 0;
        } else {
            stream_index = absolute_index - 1;
        }
        _reassembler.push_substring(static_cast<string>(payload.str()), stream_index, eof); //eof怎么判断？ FIN前面那个segment的eof为1？
    } 

    //FIN_RECV阶段 （第三次挥手后~）
    //应该是前三次挥手已经结束，这个阶段收到第四次挥手的数据包？
    //这个阶段什么也不做暂时？
    return;
}




//capacity    TCPReceiver->StreamReassembler->ByteStream(BufferList->deque<Buffer>)
//ByteStream是按顺序的字节流，包括读的部分和未读的部分，有方法_remaining_capacity，就是用capacity - _size。 _size是未读的字节流的大小
//StreamReassembler除了包含未读的ByteStream的顺序字节流，还有未重组的子字符串。StreamReaasembler调用ByteStream.output(string)写入有序字符串
//TCPReceiver接收segment，并往StreamReassembler里push_substring子字符串。在StreamReassembler的push_substring应该判断添加的子字符串是否超过capacity
size_t TCPReceiver::window_size() const { 
    //window_size = capacity - ByteStream中的有序字符串字节数
    return _reassembler.stream_out().remaining_capacity();
    //return _capacity - _reassembler.stream_out().unread_size(); //ByteStream.unread_size 就是有序的未读的字节数
}
