#pragma once

#include <sys/time.h>

class CTimer
{
public:
	CTimer(bool start = false) : m_started(false), m_accum(0)
	{
		if (start)
			Start();
	}

	void Start()
	{
		m_started = true;
		m_startTime = now();
	}

	void Stop()
	{
		m_started = false;
		m_accum += now() - m_startTime;
	}

	double Elapsed()
	{
		if (m_started)
			return (m_accum + now() - m_startTime) * 1e-6;
		else
			return m_accum * 1e-6;
	}

	double Mrps(unsigned long long r)
	{
		return r / Elapsed() * 1e-6;
	}

private:
	bool m_started;
	unsigned long long m_accum;
	unsigned long long m_startTime;

	unsigned long long now()
	{
		struct timeval tv;
		gettimeofday(&tv, 0);
		return ((unsigned long long) tv.tv_sec) * 1000000 + tv.tv_usec;
	}
};
