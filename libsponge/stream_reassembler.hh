#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <string>
#include <utility>
#include <algorithm>
#include <set>

using namespace std;

typedef pair<string, uint64_t>  StringIndex;

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes
    //思路：将子字符串的(begin_index, end_index)作为pair放入到数组中进行排序，
    //排序规则首先按照begin_index小的排在前面，begin_index相同的end_index大的排在前面。
    //记录当前已经重组的字符串的tmp_index。每次push_substring的时候，先进行排序
    //然后，判断当数组中的第一个pair的begin_index小于tmp_index，如果小于就将该子字符串处理后
    //添加到ByteStream中，删除这个pair后往后看时候还有满足的子字符串。同时若end_index <= tmp_index，
    //则这种子字符串不用添加到ByteStream中。
    //可以是用set<pair<string,uint64_t(index)>的数据结构，然后自定义排序，set是红黑树，插入删除都可以维持有序
    class Compare {
      public:
        bool operator() (const StringIndex& si1, const StringIndex& si2) const { //note: must add const for set compare (c++17)
          string s1 = si1.first;
          string s2 = si2.first;
          if (si1.second == si2.second) {
            return s1.size() > s2.size();
          } else {
            return si1.second < si2.second;
          }
        }
    };


    uint64_t _index; //表示当前重组的子字符串的index
    bool _eof;
    set<StringIndex, Compare> st{};

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);
                                
    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;

    //因为_index是私有成员变量，所以写一个public的getIndex获取已经重组的字符串的下标(_index = 下一个要写入的stream_index)
    uint64_t getIndex() const { return _index; } 
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
