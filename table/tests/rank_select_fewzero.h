
#pragma once

#include <terark/fstring.hpp>
#include <terark/valvec.hpp>
#include <terark/util/refcount.hpp>
#include <terark/int_vector.hpp>
#include <terark/io/DataIO.hpp>
#include <terark/io/StreamBuffer.hpp>
#include <terark/util/fstrvec.hpp>
#include <boost/intrusive_ptr.hpp>
#include <memory>

using terark::valvec;
using terark::febitvec;


#define RET_IF_ZERO(v) \
  do {                    \
    if ((v) == 0)         \
      return 0;           \
  } while(0)

// make sure at least one '1' and at least one '0'
class rank_select_fewzero {
public:
  typedef boost::mpl::false_ is_mixed;
rank_select_fewzero() : m_size(0) {}
  explicit
    rank_select_fewzero(size_t sz) : m_size(sz), m_bitarr(sz, true) {}

  void clear() { m_size = 0; }
  void risk_release_ownership() {}
  void risk_mmap_from(unsigned char* base, size_t length) {
    // TBD: ? m_size = *((size_t*)base);
    m_pospool.risk_set_data((size_t*)base, length);
  }
  void shrink_to_fit() {}

  void resize(size_t newsize) {
    m_size = newsize;
  }
  void swap(rank_select_fewzero& another) {
    std::swap(m_size, another.m_size);
    std::swap(m_pospool, another.m_pospool);
  }
  void set0(size_t pos) { m_bitarr.set0(pos); }
  void set1(size_t pos) { m_bitarr.set1(pos); }
  void build_cache(bool, bool) {
    // reserve 0.01
    assert(m_size > 0);
    // TBD: for test, just resize to m_size now
    //m_pospool.resize((m_size + 99) / 100 + 1);
    m_pospool.resize(m_size + 1);
    m_pospool[0] = 0;
    size_t idx = 1;
    for (size_t pos = 0; pos < m_size; pos++) {
      if (m_bitarr.is0(pos)) {
        m_pospool[idx] = pos;
        idx ++;
      }
    }
    m_pospool.resize(idx);
  }

  size_t mem_size() const { return m_pospool.used_mem_size(); }

  // exclude pos
  size_t rank0(size_t pos) const {
    assert(pos < m_size);
    RET_IF_ZERO(pos);
    pos --;
    size_t rank = terark::lower_bound_n(m_pospool, 1, m_pospool.size(), pos);
    if (rank == m_pospool.size()) {
      return m_pospool.size() - 1;
    } else if (pos == m_pospool[rank]) {
      return rank;
    } else { // < 
      return rank - 1;
    }
  }
  size_t rank1(size_t pos) const {
    assert(pos < m_size);
    return pos - rank0(pos);
  }
  size_t select0(size_t id) const {
    assert(id < m_size);
    if (id >= m_pospool.size())
      return size_t(-1);
    return m_pospool[id];
  }
  /*
   * pos0 = rank0[pos0] + rank1[pos0] - 1 => 
   *   rank1[pos0] = pos0 - rank0[pos0] + 1 =>
   *   rank1[pos0] = m_pospool[id] - id + 1, to find id? =>
   * j = m_pospool[i] - i + 1, find lower_bound() that
   * id <= m_pospool[i] - i + 1,
   * if id == m_pospool[i] - i + 1:
   *   return m_pospool[i] - i
   * elif id < m_pospool[i] - i + 1:
   *   return (id - (m_pospool[i - 1] - (i - 1) + 1)) + m_pospool[i - 1]
   */
  size_t select1(size_t id) const {
    RET_IF_ZERO(id);
    size_t i = lower_bound(1, m_pospool.size(), id);
    // TBD: possible consecutive m_pos[i] + 1 == m_pos[i + 1]
    // make sure arr[m_pospool[i] - 1] is '1' not '0'
    if (i < m_pospool.size() && id == rank1_from_rank0(i))
      return m_pospool[i] - 1;
    if (i >= m_pospool.size()) { // beyond the last '0'
      i = m_pospool.size() - 1;
    } else { // move to prev '0'
      i --; 
    }
    size_t prev = rank1_from_rank0(i);
    size_t offset = id - prev;
    return m_pospool[i] + offset;
  }

private:
  size_t rank1_from_rank0(size_t i) const {
    assert(i < m_pospool.size());
    return m_pospool[i] - i + 1;
  }
  /*
   * return rank0 that m_pospool[rank0] >= rank1's position
   */
  size_t lower_bound(size_t low, size_t upp, const size_t& rank1) const {
    size_t i = low, j = upp;
    while (i < j) {
      size_t mid = (i + j) / 2;
      size_t t = rank1_from_rank0(mid);
      if (t < rank1)
        i = mid + 1;
      else
        j = mid;
    }
    return i;
  }
  
  /*
   * res: as the rank0 lower_bound
   */
  bool is1(size_t pos, size_t& res) const {
    assert(pos < m_size);
    res = terark::lower_bound_n(m_pospool, 1, m_pospool.size(), pos);
    if (res >= m_pospool.size()) { // not in '0's
      return true;
    } else {
      return (pos != m_pospool[res]);
    }
  }

public:
  size_t max_rank0() const { return m_pospool.size() - 1; }
  size_t max_rank1() const { return m_size - (m_pospool.size() - 1); }
  size_t size() const { return m_size; }

  const void* data() const { return m_pospool.data(); }
  bool operator[](size_t pos) const {
    assert(pos < m_size);
    return is1(pos);
  }
  // TBD: use hash_set maybe better
  bool is1(size_t pos) const {
    assert(pos < m_size);
    size_t res;
    return is1(pos, res);
  }
  bool is0(size_t pos) const {
    assert(pos < m_size);
    return !is1(pos);
  }

  const uint32_t* get_rank_cache() const { return NULL; }
  const uint32_t* get_sel0_cache() const { return NULL; }
  const uint32_t* get_sel1_cache() const { return NULL; }

  ///@returns number of continuous one/zero bits starts at bitpos
  // TBD: has one prev state stored, which may helpful to avoid
  //  invoking is1() during Next()/Prev()?
  size_t zero_seq_len(size_t pos) const {
    assert(pos < m_size);
    size_t i;
    if (is1(pos, i))
      return 0;
    size_t cnt = 1;
    while (i < m_pospool.size() - 1) {
      if (m_pospool[i] + 1 == m_pospool[i + 1])
        i++, cnt++;
      else
        break;
    }
    return cnt;
  }
  size_t zero_seq_revlen(size_t pos) const {
    assert(pos < m_size);
    RET_IF_ZERO(pos);
    pos --;
    size_t i;
    if (is1(pos, i))
      return 0;
    size_t cnt = 1;
    while (i > 1) { // m_pospool[0] is placeholder
      if (m_pospool[i - 1] + 1 == m_pospool[i])
        i--, cnt++;
      else
        break;
    }
    return cnt;
  }
  size_t one_seq_len(size_t pos) const {
    assert(pos < m_size);
    size_t i;
    if (!is1(pos, i))
      return 0;
    if (i >= m_pospool.size() - 1)
      return m_size - pos;
    else
      return m_pospool[i] - pos;
  }
  size_t one_seq_revlen(size_t pos) const {
    assert(pos < m_size);
    RET_IF_ZERO(pos);
    pos --;
    size_t i;
    if (!is1(pos, i))
      return 0;
    if (i == 1)
      return pos + 1;
    else
      return pos - m_pospool[i - 1];
  }

private:
  valvec<size_t> m_pospool;
  size_t m_size;
  febitvec m_bitarr;
};
