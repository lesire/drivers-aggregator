#include "TimestampEstimator.hpp"
#include <limits.h> //for INT_MAX
#include <iostream>
#include <stdexcept>

using namespace aggregator;

TimestampEstimator::TimestampEstimator(base::Time window,
				       base::Time initial_period,
				       base::Time min_latency,
				       int lost_threshold)
    : m_window(window.toSeconds()), m_lost_threshold(lost_threshold)
    , m_lost(0), m_lost_total(0), m_min_offset(0), m_min_offset_reset(0)
    , m_latency(0)
    , m_min_latency(min_latency.toSeconds())
    , m_initial_period(initial_period.toSeconds())
    , m_missing_samples(0)
    , m_last_index(0)
    , m_have_last_index(false)
{
}

TimestampEstimator::TimestampEstimator(base::Time window,
				       base::Time initial_period,
				       int lost_threshold)
    : m_window(window.toSeconds()), m_lost_threshold(lost_threshold)
    , m_lost(0), m_lost_total(0), m_min_offset(0), m_min_offset_reset(0)
    , m_latency(0)
    , m_min_latency(0)
    , m_initial_period(initial_period.toSeconds())
    , m_missing_samples(0)
    , m_last_index(0)
    , m_have_last_index(false)
{
}

TimestampEstimator::TimestampEstimator(base::Time window,
				       int lost_threshold)
    : m_window(window.toSeconds()), m_lost_threshold(lost_threshold)
    , m_lost(0), m_lost_total(0), m_min_offset(0), m_min_offset_reset(0)
    , m_initial_period(-1)
    , m_missing_samples(0)
    , m_last_index(0)
    , m_have_last_index(false)
{
}

base::Time TimestampEstimator::getPeriod() const
{ return base::Time::fromSeconds(getPeriodInternal()); }
double TimestampEstimator::getPeriodInternal() const
{
    int count = m_samples.size();
    std::list<double>::const_reverse_iterator b;
    //ignore lost samples(value <= 0) at the end of m_samples
    for(b = m_samples.rbegin();	*b <= 0 && b != m_samples.rend(); b++, count--)
    {}
    return (*b - m_samples.front()) / (count - 1);
}

int TimestampEstimator::getLostSampleCount() const
{ return m_lost_total; }

void TimestampEstimator::shortenSampleList(base::Time time)
{
    double current = time.toSeconds();

    if (haveEstimate())
    {
	// Compute the period up to now for later reuse
	double period = getPeriodInternal();

	//scan forward until we hit the window size
	std::list<double>::iterator end;
	end = m_samples.begin();
	double min_time = current - m_window;
	while(end != m_samples.end() && *end < min_time)
	    end++;

	std::list<double>::iterator window_begin = end;

	//scan backward until we find a gap that is at least period sized.
	//that should be the last sample from a burst, giving better
	//period estimation
	std::list<double>::iterator last_good = end;
	int smp_count = 0;
	while(end != m_samples.begin())
	{
	    if (*end > 0)
	    {
		if (smp_count > 0 && (*last_good-*end) / smp_count >= period)
		    break;

		last_good = end;
		smp_count = 0;
	    }
	    end--;
	    smp_count++;
	}

	//if we didn't find anything and the buffer is too large already,
	//fall back to real window begin
	if (end == m_samples.begin() && *end < min_time - m_window)
	    end = window_begin;

	//scan forward again as long as we find lost samples
	for(;end != m_samples.end() && *end <= 0; end++) {}

	std::list<double>::iterator it;
	for(it = m_samples.begin(); it != end; it++) {
	    if (*it <= 0)
		m_missing_samples--;
	}

	m_samples.erase(m_samples.begin(), end);
    }
    //std::cerr << "samples left: " << m_samples.size()
    //          << " of these, missing: " << m_missing_samples
    //          << " time: " << (m_samples.back()-m_samples.front())
    //          << "\n";

    if (m_samples.size() == m_missing_samples)
    {
	m_samples.clear();
	m_missing_samples = 0;
    }
}

base::Time TimestampEstimator::update(base::Time time)
{
    shortenSampleList(time);

    double current = time.toSeconds();

    // Add the new sample, and return if we don't have at least two samples
    // (either by having been added or by adding them now)
    if (m_samples.empty())
    {
        m_last = current - m_latency;
        m_min_offset_reset = current;

	if (m_initial_period > 0) {
	    for(int n = 1; m_initial_period * n <= m_window; n++) {
		m_samples.push_front(current - m_initial_period * n);
	    }
	}
    }

    m_samples.push_back(current);

    if (!haveEstimate())
	return time;

    // Recompute the period
    double period = getPeriodInternal();

    // Check for lost samples
    int sample_distance = round((current - m_last - m_latency) / period);
    if (sample_distance > 1 && m_lost_threshold != INT_MAX)
        m_lost.push_back(sample_distance - 1);
    else
        m_lost.clear();

    if (static_cast<int>(m_lost.size()) > m_lost_threshold)
    {
        int lost_count = *std::min(m_lost.begin(), m_lost.end());
        m_lost_total += lost_count;
        m_lost.clear();
        //std::cout << "lost " << lost_count << " samples" << std::endl;
        //std::cout << "  original current-last ==" << current - m_last + m_latency << std::endl;
        //std::cout << "  period ==" << period << std::endl;
        m_last = m_last + period * lost_count;
        //std::cout << "  updated current-last ==" << current - m_last << std::endl;
    }

    m_last = m_last + period;
    m_min_offset = std::min(m_min_offset, current - (double)m_last - m_latency);

    if (m_min_offset < 0 || current - m_min_offset_reset > m_window)
    {
	if (m_min_offset < 0)
	    //need to clear because m_last will "jump" backwards
	    m_lost.clear();

        m_last += m_min_offset;
        m_min_offset = period;
        m_min_offset_reset = current;
    }

    return base::Time::fromSeconds((double)m_last);
}

base::Time TimestampEstimator::updateLoss()
{
    m_samples.push_back(-1);
    m_missing_samples++;

    if (haveEstimate()) {
	double period = getPeriodInternal();
	m_last = m_last + period;
    }
    return base::Time::fromSeconds((double)m_last);
}

void TimestampEstimator::updateReference(base::Time ts)
{
    if (!haveEstimate())
	return;

    double period = getPeriodInternal();

    double time = ts.toSeconds();
    //adjust m_latency and m_last so that:
    //mlast' + m_latency' = m_last + m_latency  //this is to not trip update()
    //m_latency' >= m_min_latency
    //m_latency' < m_min_latency + period
    //m_last' = ts + n*period | n \in \Z

    //m_last' = ts + n*period

    //m_latency' - m_last - m_latency =  - ts - n*period
    //m_latency' - m_last - m_latency >= m_min_latency - m_last - m_latency
    //m_latency' - m_last - m_latency < m_min_latency + period - m_last - m_latency

    // n <= (m_last + m_latency - ts - m_min_latency)/period
    // n > (m_last + m_latency - ts - m_min_latency)/period - 1

    //this could probably use a more gradual approach
    int n;
    n = (m_last + m_latency - time - m_min_latency)/period;

    m_latency = m_last + m_latency - time - n * period;
    m_last = time + n * period;
}

bool TimestampEstimator::haveEstimate() const
{
    return m_samples.size() - m_missing_samples >= 2;
}

base::Time TimestampEstimator::update(base::Time time, int index)
{
    int lost = 0;
    if (!m_have_last_index)
	m_have_last_index = true;
    else
	lost = index - m_last_index - 1;
    m_last_index = index;

    while(lost > 0)
    {
	lost--;
	updateLoss();
    }
    return update(time);
}
