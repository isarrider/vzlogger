/**
 * Per-channel reading log (threadsafe)
 *
 * Used to store recent readings and buffer them in case of net inconnectivity.
 *
 * Producer (reading thread):
 *  - push() stages the raw readings of the current aggregation period.
 *  - publish() is called once per period. It aggregates the staged readings (if an aggmode is
 *    set) and appends the result to the log. Every entry in the log gets a monotonically
 *    increasing sequence number. The entries published in this call are returned, so that
 *    consumers living in the reading thread (local httpd, MQTT) see each entry exactly once.
 *
 * Sender (logging thread, exactly one per channel):
 *  - read() copies entries starting at a cursor (a sequence number) without removing them.
 *  - commit() acknowledges all entries before a cursor. Only acknowledged entries are removed,
 *    so unacknowledged entries survive network outages of any length, limited by max_size()
 *    (when exceeded, the oldest entries are dropped).
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

#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <deque>
#include <pthread.h>
#include <stdint.h>
#include <string>
#include <vector>

#include <Reading.hpp>

class Buffer {

  public:
	typedef vz::shared_ptr<Buffer> Ptr;
	typedef uint64_t Cursor; /**< sequence number of the next entry a consumer wants */

	struct Entry {
		Cursor seq;      /**< position in the log, strictly increasing */
		int64_t time_ms; /**< timestamp of the reading [ms] */
		double value;    /**< value of the reading */
	};

	enum aggmode { NONE, MAX, AVG, SUM };

	Buffer();
	virtual ~Buffer();

	/* producer (reading thread) */

	/**
	 * Stage a raw reading of the current aggregation period.
	 * It becomes visible to consumers with the next publish().
	 */
	void push(const Reading &rd);

	/**
	 * Aggregate the staged readings (if aggmode != NONE) and append the result to the log.
	 *
	 * @param aggtime length of the aggregation period [s] (used for aggFixedInterval)
	 * @param aggFixedInterval round timestamps of aggregated entries down to aggtime
	 * @param published if not NULL, the entries appended by this call are stored here
	 * @return number of entries appended
	 */
	size_t publish(int aggtime, bool aggFixedInterval, std::vector<Entry> *published = NULL);

	/* sender (logging thread) */

	/**
	 * Copy up to max entries with seq >= from into out (out is cleared first).
	 *
	 * @param lost if not NULL, receives the number of entries >= from that are no longer
	 *     available because they were dropped (max_size exceeded)
	 * @return number of entries copied
	 */
	size_t read(Cursor from, size_t max, std::vector<Entry> &out, uint64_t *lost = NULL);

	/**
	 * Acknowledge all entries with seq < next. They are removed from the log.
	 */
	void commit(Cursor next);

	/** sequence number the next published entry will get */
	Cursor end_cursor();

	/**
	 * Limit the number of entries kept in the log. 0 means unlimited (default).
	 * If the limit is exceeded, the oldest entries are dropped, whether acknowledged or not.
	 */
	void set_max_size(size_t n);
	size_t max_size();

	/** number of entries in the log (published, not yet acknowledged) */
	size_t size();
	std::string dump();

	/* synchronisation between reading and logging thread (see Channel::wait/notify) */

	inline bool newValues() const { return _newValues; }
	inline void clear_newValues() { _newValues = false; }

	inline void lock() { pthread_mutex_lock(&_mutex); }
	inline void unlock() { pthread_mutex_unlock(&_mutex); }
	inline void wait(pthread_cond_t *condition) { pthread_cond_wait(condition, &_mutex); }

	/** name of the channel, used in log messages */
	inline void set_name(const std::string &name) { _name = name; }

	inline void set_aggmode(Buffer::aggmode m) { _aggmode = m; }
	inline aggmode get_aggmode() const { return _aggmode; }

  private:
	Buffer(const Buffer &);            // don't allow copy constructor
	Buffer &operator=(const Buffer &); // and no assignment op.

	struct Raw {
		int64_t time_ms;
		double value;
	};

	void append(int64_t time_ms, double value); // _mutex must be held
	void enforce_max_size();                    // _mutex must be held

	std::vector<Raw> _staged; /**< raw readings of the current aggregation period */
	std::deque<Entry> _log;   /**< published entries, oldest first */
	Cursor _next_seq;         /**< seq of the next entry to publish */
	size_t _max_size;         /**< 0: unlimited */
	uint64_t _dropped;        /**< total number of entries dropped due to _max_size */

	bool _newValues;
	Buffer::aggmode _aggmode;

	// keeps value and time of the latest raw reading of the previous period, for aggmode AVG
	bool _have_last_avg;
	Raw _last_avg;

	std::string _name;
	pthread_mutex_t _mutex;
};

#endif /* _BUFFER_H_ */
