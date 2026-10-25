/**
 * Sender-side view of a channel's Buffer
 *
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

#include <api/BufferReader.hpp>
#include <common.h>

vz::api::BufferReader::BufferReader(Buffer::Ptr buf, const char *name, int duplicates_s,
									bool allow_equal_timestamps)
	: _buf(buf), _name(name ? name : ""), _duplicates_ms((int64_t)duplicates_s * 1000),
	  _allow_equal_timestamps(allow_equal_timestamps), _cursor(0), _batch_end(0), _first_end(0) {
	// _cursor 0: start with the oldest entry in the buffer
	_filter.last_ts = 0;
	_filter.have_last = false;
	_filter.last_value = 0;
	_batch_filter = _first_filter = _filter;
}

bool vz::api::BufferReader::accept(Filter &f, const Buffer::Entry &e) const {
	// we can only add/consider a timestamp if the ms resolution is different than from the
	// previous one (or, if allowed, the same):
	if (_allow_equal_timestamps ? (e.time_ms < f.last_ts) : (e.time_ms <= f.last_ts))
		return false;

	if (_duplicates_ms == 0) { // send all values
		f.last_ts = e.time_ms;
		return true;
	}
	// duplicates should be ignored, but send at least each <duplicates> seconds
	if (!f.have_last || e.time_ms >= f.last_ts + _duplicates_ms || e.value != f.last_value) {
		f.have_last = true;
		f.last_value = e.value;
		f.last_ts = e.time_ms;
		return true;
	}
	return false;
}

const std::vector<Buffer::Entry> &vz::api::BufferReader::next(size_t max) {
	_batch.clear();
	Filter f = _filter;
	Buffer::Cursor c = _cursor;
	std::vector<Buffer::Entry> chunk;

	while (_batch.size() < max) {
		uint64_t lost = 0;
		size_t n = _buf->read(c, max, chunk, &lost);
		if (lost > 0) {
			// dropped by the Buffer (max size exceeded, it logs a warning), can't be sent anymore
			print(log_debug, "%llu readings were dropped before they could be sent", _name.c_str(),
				  (unsigned long long)lost);
			c += lost;
			if (_cursor < c)
				_cursor = c; // don't report them again (filter state is not affected)
		}
		if (n == 0)
			break;
		for (std::vector<Buffer::Entry>::const_iterator it = chunk.begin();
			 it != chunk.end() && _batch.size() < max; ++it) {
			c = it->seq + 1;
			if (accept(f, *it)) {
				_batch.push_back(*it);
				if (_batch.size() == 1) {
					_first_end = c;
					_first_filter = f;
				}
			}
		}
		if (n < max)
			break; // no more entries
	}
	_batch_end = c;
	_batch_filter = f;

	if (_batch.empty() && _batch_end != _cursor) {
		print(log_finest, "skipped %llu readings (duplicates/timestamps)", _name.c_str(),
			  (unsigned long long)(_batch_end - _cursor));
		advance(_batch_end, _batch_filter);
	}
	return _batch;
}

void vz::api::BufferReader::advance(Buffer::Cursor c, const Filter &f) {
	_cursor = c;
	_filter = f;
	_buf->commit(_cursor);
}

void vz::api::BufferReader::commit() {
	advance(_batch_end, _batch_filter);
	_batch.clear();
}

void vz::api::BufferReader::commit_first() {
	if (_batch.empty())
		return;
	advance(_first_end, _first_filter);
	_batch.clear();
}
