/**
 * Per-channel reading log (see Buffer.hpp)
 *
 * Used to store recent readings and buffer them in case of net inconnectivity
 *
 * @author Steffen Vogel <info@steffenvogel.de>
 * @copyright Copyright (c) 2011 - 2023, The volkszaehler.org project
 * @package vzlogger
 * @license http://opensource.org/licenses/gpl-license.php GNU Public License
 */
/*
 * This file is part of volkzaehler.org
 *
 * volkzaehler.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * volkzaehler.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include "float.h" /* double min max */
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "Buffer.hpp"

Buffer::Buffer()
	: _next_seq(0), _max_size(0), _dropped(0), _newValues(false), _aggmode(NONE),
	  _have_last_avg(false) {
	_last_avg.time_ms = 0;
	_last_avg.value = 0;
	pthread_mutex_init(&_mutex, NULL);
}

Buffer::~Buffer() { pthread_mutex_destroy(&_mutex); }

void Buffer::push(const Reading &rd) {
	Raw raw;
	raw.time_ms = rd.time_ms();
	raw.value = rd.value();
	lock();
	_staged.push_back(raw);
	unlock();
}

void Buffer::append(int64_t time_ms, double value) {
	Entry e;
	e.seq = _next_seq++;
	e.time_ms = time_ms;
	e.value = value;
	_log.push_back(e);
}

void Buffer::enforce_max_size() {
	if (_max_size == 0 || _log.size() <= _max_size)
		return;
	size_t n = _log.size() - _max_size;
	_log.erase(_log.begin(), _log.begin() + n);
	_dropped += n;
	print(log_warning,
		  "Buffer full (max_size %zu): dropped %zu oldest readings (%llu in total). This "
		  "indicates a connection problem.",
		  _name.c_str(), _max_size, n, (unsigned long long)_dropped);
}

size_t Buffer::publish(int aggtime, bool aggFixedInterval, std::vector<Entry> *published) {
	lock();
	const Cursor first_new = _next_seq;

	if (_staged.empty()) {
		// nothing to publish
	} else if (_aggmode == NONE) {
		for (std::vector<Raw>::const_iterator it = _staged.begin(); it != _staged.end(); ++it)
			append(it->time_ms, it->value);
	} else {
		// all aggregation modes produce one entry with the timestamp of the latest reading
		std::vector<Raw>::const_iterator latest = _staged.begin();
		for (std::vector<Raw>::const_iterator it = _staged.begin(); it != _staged.end(); ++it) {
			if (it->time_ms > latest->time_ms)
				latest = it;
		}

		double aggvalue = 0;
		if (_aggmode == MAX) {
			aggvalue = -DBL_MAX;
			for (std::vector<Raw>::const_iterator it = _staged.begin(); it != _staged.end(); ++it)
				aggvalue = std::max(aggvalue, it->value);
		} else if (_aggmode == SUM) {
			for (std::vector<Raw>::const_iterator it = _staged.begin(); it != _staged.end(); ++it)
				aggvalue += it->value;
		} else { // AVG
			// time-weighted average: each value is valid until the next reading. The latest
			// reading of the previous period is the starting point, the latest reading of this
			// period has no duration yet (it is the starting point of the next period).
			// Staged readings are in the order the meter delivered them (assumed sorted by time).
			double sum = 0.0, timespan = 0.0;
			bool have_prev = _have_last_avg;
			Raw prev = _last_avg;
			for (std::vector<Raw>::const_iterator it = _staged.begin(); it != _staged.end(); ++it) {
				if (have_prev) {
					double dt = (double)(it->time_ms - prev.time_ms) / 1000.0;
					sum += prev.value * dt;
					timespan += dt;
				}
				prev = *it;
				have_prev = true;
			}
			aggvalue = (timespan > 0.0) ? sum / timespan : latest->value;
			_last_avg = *latest;
			_have_last_avg = true;
		}

		int64_t time_ms = latest->time_ms;
		if (aggFixedInterval && aggtime > 0) {
			// round down to the start of the aggregation interval (full seconds)
			time_ms = (int64_t)aggtime * ((time_ms / 1000) / aggtime) * 1000;
		}
		append(time_ms, aggvalue);
	}
	_staged.clear();

	enforce_max_size();

	size_t n = _next_seq - first_new;
	if (published) {
		published->clear();
		for (std::deque<Entry>::const_iterator it = _log.begin(); it != _log.end(); ++it) {
			if (it->seq >= first_new)
				published->push_back(*it);
		}
	}
	_newValues = true;
	unlock();
	return n;
}

size_t Buffer::read(Cursor from, size_t max, std::vector<Entry> &out, uint64_t *lost) {
	out.clear();
	lock();
	const Cursor first = _log.empty() ? _next_seq : _log.front().seq;
	if (lost)
		*lost = (from < first) ? first - from : 0;
	if (from < first)
		from = first;
	// entries in _log have consecutive sequence numbers, so the position can be computed
	size_t pos = from - first;
	for (; pos < _log.size() && out.size() < max; ++pos)
		out.push_back(_log[pos]);
	unlock();
	return out.size();
}

void Buffer::commit(Cursor next) {
	lock();
	while (!_log.empty() && _log.front().seq < next)
		_log.pop_front();
	unlock();
}

Buffer::Cursor Buffer::end_cursor() {
	lock();
	Cursor c = _next_seq;
	unlock();
	return c;
}

void Buffer::set_max_size(size_t n) {
	lock();
	_max_size = n;
	enforce_max_size();
	unlock();
}

size_t Buffer::max_size() {
	lock();
	size_t n = _max_size;
	unlock();
	return n;
}

size_t Buffer::size() {
	lock();
	size_t n = _log.size();
	unlock();
	return n;
}

std::string Buffer::dump() {
	std::ostringstream o;
	o << '{' << std::setprecision(4);
	lock();
	for (std::deque<Entry>::const_iterator it = _log.begin(); it != _log.end(); ++it) {
		if (it != _log.begin())
			o << ',';
		o << it->value;
	}
	unlock();
	o << '}';
	return o.str();
}
