/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDPROCESSINGCHAIN_H
#define GOSOUNDPROCESSINGCHAIN_H

#include <memory>
#include <vector>

class GOSoundProcessingChainState;
class GOSoundProcessingPrmMapper;
class GOSoundProcessor;

/**
 * An ordered list of GOSoundProcessor%s, run in that order by every
 * GOSoundProcessingChainState created from this chain, plus the
 * GOSoundProcessingPrmMapper%s that keep those processors' parameters up to
 * date. Owns both lists as two independent vectors rather than a paired
 * struct: cardinality between processors and mappers is not 1:1 (a
 * processor may need no mapper) and mapper order carries no meaning, unlike
 * processor order, which is the DSP signal path.
 */
class GOSoundProcessingChain {
private:
  friend class GOTestSoundProcessingChain; // direct access for unit tests

  // Owned processors, in signal-path order; Process() walks them in this
  // order.
  std::vector<std::unique_ptr<GOSoundProcessor>> m_processors;
  // Owned mappers; order carries no meaning (each touches disjoint
  // parameters of the processors above).
  std::vector<std::unique_ptr<GOSoundProcessingPrmMapper>> m_mappers;

public:
  GOSoundProcessingChain();
  ~GOSoundProcessingChain();

  GOSoundProcessingChain(const GOSoundProcessingChain &) = delete;
  GOSoundProcessingChain &operator=(const GOSoundProcessingChain &) = delete;

  /** Appends a processor; it will run after all previously added ones. */
  void AddProcessor(std::unique_ptr<GOSoundProcessor> pProcessor);

  /** Appends a mapper; mapper order is irrelevant (they touch disjoint
   * parameters). */
  void AddMapper(std::unique_ptr<GOSoundProcessingPrmMapper> pMapper);

  unsigned GetNProcessors() const { return (unsigned)m_processors.size(); }

  /** @return whether this chain has no processors (an empty state's Process()
   * is then a no-op) */
  bool IsEmpty() const { return m_processors.empty(); }

  /**
   * @param processorI Position in the signal path (0-based)
   * @return the processor at that position
   */
  const GOSoundProcessor &GetProcessor(unsigned processorI) const;

  /** Forwards EnsureSetup() to every processor, in order. */
  void EnsureSetup(unsigned nChannels, unsigned nFrames, unsigned sampleRate);

  /** Forwards EnsureParametersUpToDate() to every mapper. */
  void EnsureParametersUpToDate();

  /** @return a new DSP-memory instance of this chain: one state per processor
   */
  std::unique_ptr<GOSoundProcessingChainState> CreateState() const;
};

#endif /* GOSOUNDPROCESSINGCHAIN_H */
