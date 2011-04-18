#ifndef AGGREGATOR_TIMESTAMP_SYNCHRONIZER_HPP
#define AGGREGATOR_TIMESTAMP_SYNCHRONIZER_HPP

#include <base/time.h>
#include <list>
#include <TimestampEstimator.hpp>

namespace aggregator
{
    template<class Item>
    class TimestampSynchronizer
    {
    private:
	struct ItemInfo
	{
	    Item item;
	    base::Time time;
	};
	std::list<ItemInfo> m_items;
	std::list<base::Time> m_refs;
	base::Time m_maxItemLatency;
	base::Time m_matchWindowOldest;
	base::Time m_matchWindowNewest;
	bool m_useEstimator;

	TimestampEstimator tsestimator;
    public:
	/** Constructs a TimestampSynchronizer
	 * @param maxItemLatency     Maximum age of items in the internal list
	 * @param matchWindowOldest  The oldest relative time at which a
	 *                           given reference timestamp matches an item
	 *                           time
	 * @param matchWindowNewest  The newest relative time at which a
	 *                           given reference timestamp matches an item
	 *                           time
	 * @param estimatorWindow    The window size to use to estimate
	 *                           lost reference timestamps, 0 means not
	 *                           using the estimator at all
	 *                           see TimestampEstimator
	 * @param estimatorInitiailPeriod  The initial period for the estimator
	 *                           see TimestampEstimator
	 * @param estimatorLostThreashold  The lost threshold for the estimator
	 *                           see TimestampEstimator
	 */
	TimestampSynchronizer(base::Time const & maxItemLatency,
			      base::Time const & matchWindowOldest,
			      base::Time const & matchWindowNewest,
			      base::Time const & estimatorWindow,
			      base::Time const & estimatorInitialPeriod
			      = base::Time::fromSeconds(-1),
			      int estimatorLostThreshold = 2);

	/** Push an item, time pair into the internal list. */
	void pushItem(Item const &item, base::Time const & time);

	/** Push a reference timestamp into the internal list. */
	void pushReference(base::Time const & ref);

	/** Fetch a synchronized item, time pair from the internal lists, using
	 *  now and maxItemLatency to determine lost reference timestamps.
	 */
	bool fetchItem(Item &item, base::Time & time, base::Time const & now);
    };

    template<class Item>
    TimestampSynchronizer<Item>::TimestampSynchronizer
    (base::Time const & maxItemLatency,
     base::Time const & matchWindowOldest,
     base::Time const & matchWindowNewest,
     base::Time const & estimatorWindow,
     base::Time const & estimatorInitialPeriod,
     int estimatorLostThreshold)
	: m_maxItemLatency(maxItemLatency)
	, m_matchWindowOldest(matchWindowOldest)
	, m_matchWindowNewest(matchWindowNewest)
	, m_useEstimator(estimatorWindow != base::Time::fromMicroseconds(0))
	, tsestimator(estimatorWindow,
		      estimatorInitialPeriod,
		      estimatorLostThreshold)
    {
    }

    template<class Item>
    void TimestampSynchronizer<Item>::pushItem(Item const &item, base::Time const & time)
    {
	m_items.push_back(ItemInfo());
	m_items.back().item = item;
	m_items.back().time = time;
    }

    template<class Item>
    void TimestampSynchronizer<Item>::pushReference(base::Time const & ref)
    {
	//cascading a TimestampEstimator here gives a nicer estimate
	m_refs.push_back(ref);

	//clear out unneeded refs
	while(!m_refs.empty() && !m_items.empty() &&
	      m_refs.front() + m_matchWindowOldest < m_items.front().time)
	{
	    if (m_refs.front() + m_matchWindowNewest > m_items.front().time)
	    {
		//got a match
		break;
	    }

	    m_refs.pop_front();
	}
    }

    template<class Item>
    bool TimestampSynchronizer<Item>::fetchItem(Item &item,
						base::Time & time,
						base::Time const & now)
    {
	//drop TS in m_refs that are before the oldest m_items TS minus
	//maximum latency for matching the item.
	while(!m_refs.empty() && !m_items.empty() &&
	      m_refs.front() + m_matchWindowOldest < m_items.front().time)
	{
	    if (m_refs.front() + m_matchWindowNewest > m_items.front().time)
	    {
		//got a match
		item = m_items.front().item;
		time = m_refs.front();
		if (m_useEstimator)
		    tsestimator.update(time);
		m_items.pop_front();
		m_refs.pop_front();
		return true;
	    }

	    m_refs.pop_front();
	}

	//finally, send all m_items that sit in our buffer and are too old on their way(with the guessed timestamp)
	if((!m_items.empty() && m_items.front().time < now - m_maxItemLatency) ||
	   (!m_refs.empty() && !m_items.empty() &&
	    m_refs.front() + m_matchWindowOldest >= m_items.front().time))
	{
	    item = m_items.front().item;
	    time = m_items.front().time;
	    if (m_useEstimator)
	    {
		if(tsestimator.haveEstimate())
		    time = tsestimator.updateLoss();
		else
		    tsestimator.updateLoss();
		tsestimator.shortenSampleList(time);
	    }

	    m_items.pop_front();
	    return true;
	}

	return false;
    }

}

#endif /*AGGREGATOR_TIMESTAMP_SYNCHRONIZER_HPP*/