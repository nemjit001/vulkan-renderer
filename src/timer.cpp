#include "timer.hpp"

void Timer::tick()
{
	m_last = m_current;
	m_current = Clock::now();
}

void Timer::reset()
{
	m_current = Clock::now();
	m_last = m_current;
}

double Timer::deltaTimeMS()
{
	Duration delta = m_current - m_last;
	return delta.count();
}

double Timer::timeSinceStartMS()
{
	Duration delta = m_current - m_start;
	return delta.count();
}

RunningAverage::RunningAverage(uint32_t valueCount)
	:
	m_alpha(1.0 / static_cast<double>(valueCount)),
	m_invAlpha(1.0 - m_alpha)
{
	//
}

void RunningAverage::update(double value)
{
	m_average = m_average * m_invAlpha + value * m_alpha;
}

double RunningAverage::getAverage() const
{
	return m_average;
}
