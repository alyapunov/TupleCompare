#pragma once

#include <chrono>

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
		m_startTime = std::chrono::high_resolution_clock::now();
	}

	void Stop()
	{
		m_started = false;
		timeSpan_t timeSpan = now() - m_startTime;
		m_accum += timeSpan.count();
	}

	double Elapsed()
	{
		if (m_started) {
			timeSpan_t timeSpan = now() - m_startTime;
			return m_accum + timeSpan.count();
		} else {
			return m_accum;
		}
	}

	double Mrps(unsigned long long r)
	{
		return r / Elapsed() * 1e-6;
	}

private:
	bool m_started;
	double m_accum;
	typedef std::chrono::time_point<std::chrono::high_resolution_clock> timePoint_t;
	typedef std::chrono::duration<double> timeSpan_t;
	timePoint_t m_startTime;

	static timePoint_t now()
	{
		return std::chrono::high_resolution_clock::now();
	}
};
