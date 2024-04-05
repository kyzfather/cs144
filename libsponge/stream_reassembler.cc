#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//注意，初始列表初始化顺序与类中定义顺序一直，而与初始化列表顺序无关。如果初始化列表顺序与定义顺序不一致会warning
StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), _index(0), _eof(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
//这个eof有什么用啊，带有eof标记的data后面还会发来data吗？ 当收到的TCPSegment带有FIN标记时，这里传入的eof为1？ 带有FIN标记的TCPSegment也可能是携带有数据的
//要判断是否超过capacity
//初代版本的实现没有将重叠的子字符串进行合并，只判断该子字符串的尾部是否超过了右边界，然后将子字符串放入到set集合中
//set集合中可能有大量重叠的子字符串，这样的话会占用较大的内存空间。
//正确的实现方式应该将重叠的子字符串合并，让set缓存乱序数据包的内存空间真正的大概符合capacity的要求
//---------------------------------------------------------------------------------------------------------------------
//思考一下：真正的场景中什么时候会发生 接收了两个数据包，其中一个数据包包含了另一个数据包，或者两个数据包有重叠？是什么原因造成的？
//tcp分段，选择重传？
void StreamReassembler::push_substring(const string &data, const uint64_t index, const bool eof) {
    uint64_t idx = index;
    //然后判断子字符串是否超过右边界，如果超过就需要剪裁
    uint64_t left_index = _index + _output.remaining_capacity();
    if (eof) {     //如果当前data超过了capacity，那么这个data的eof不应该被设置。
        if (idx + data.size() <= left_index)
            _eof |= eof;
    }
    string tmp_data = data;
    if (idx + data.size() > left_index) {
        tmp_data = data.substr(0, left_index - idx);
    }
    //遍历set看当前字符串是否与之前的字符串可以合并
    while (true) { //从头遍历set，如果可以合并就先合并，然后再从头开始看是否合并。
        uint64_t left1 = idx;
        uint64_t right1 = idx + tmp_data.size();
        bool merge = false;
        for(auto it = st.begin(); it != st.end(); it++) { 
            StringIndex tmp = *it;
            uint64_t left2 = tmp.second;
            uint64_t right2 = tmp.second + tmp.first.size();
            //合并的话总共就有下面5种情况
            if (right1 >= left2 && left1 <= left2 && right2 >= right1) {
                string s1 = tmp_data.substr(0, left2 - left1);
                string s2 = tmp.first;
                tmp_data = s1 + s2;
                idx = idx;
                st.erase(*it);
                merge = true;
                break;
            } else if (left1 <= left2 && right1 >= right2) {
                st.erase(*it);
                merge = true;
                break;
            } else if (left1 <= right2 && right1 >= right2 && left2 <= left1) {
                string s1 = tmp.first;
                string s2 = tmp_data.substr(right2 - left1, right1 - right2);
                tmp_data = s1 + s2;
                idx = left2;
                st.erase(*it);
                merge = true;
                break;
            } else if (left1 >= left2 && right1 <= right2) {
                tmp_data = tmp.first;
                idx = left2;
                st.erase(*it);
                merge = true;
                break;
            } else { //互不相交
                continue;
            }  
        }
        if (merge == false) //如果没有可以合并的，就退出循环
            break;
    }
    if (tmp_data != "")
        st.insert({tmp_data, idx});
    //合并之后遍历set集合中的字符串，看是否有可以续上已经重排的。实际上好像只用看第一个string就行了
    while (true) {
        if (st.empty())
            break;
        StringIndex tmp = *st.begin();
        if (tmp.second <= _index) { //说明是可以连续上的，中间没有断开的
            if (tmp.first.size() + tmp.second <= _index) { //当前子字符串已经包含在重组的比特流中了。
                st.erase(tmp);
                continue;
            }
            uint64_t start = _index - tmp.second; 
            uint64_t len =  tmp.first.size() - start;
            string tmp_string = tmp.first.substr(start, len);
            _output.write(tmp_string);
            _index += len;
            st.erase(tmp); //该子字符串处理完毕，从set中删除。
        } else { //说明当前子字符串和重组的字符串是中间断开的，需要等待后续的子字符串到来才能进行重组
            break; 
        }       
    }
    if (st.empty() && _eof) { 
        _output.end_input();
    }
        
}

//返回当前有多少子字符串的字节数目没有被重组（overlap重叠部分的bytes只计算一次）
//思路：因为没有合并子字符串，所以只能便利set中的元素。
size_t StreamReassembler::unassembled_bytes() const { 
    if (st.empty())
        return 0;
    // uint64_t tail = (*st.begin()).second;
    size_t total = 0;
    for (auto it = st.begin(); it != st.end(); it++) {
        StringIndex tmp = *it;
        total += tmp.first.size();
        // StringIndex si = *it;
        // if (si.second < tail) { //当前子字符串和上个子字符串有重叠
        //     total += static_cast<size_t>(max(uint64_t(0), si.second + static_cast<uint64_t>(si.first.size()) - tail));
        // } else {
        //     total += si.first.size();
        // }
        // tail = max(tail, si.second + static_cast<uint64_t>(si.first.size()));
    }
    return total;
}

bool StreamReassembler::empty() const { return st.empty(); }
