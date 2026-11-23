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

#ifndef _BufferReader_hpp_
#define _BufferReader_hpp_

#include <string>
#include <vector>

#include <Buffer.hpp>

namespace vz {
namespace api {

/**
 * Reads batches of entries for the (single) sender of a channel, starting at the sender's
 * cursor, and applies the channel's timestamp and duplicates filter.
 *
 * Neither the cursor nor the filter state change until commit(): if a request fails, the next
 * call to next() returns the same entries again (plus newer ones, up to max). The entries stay
 * in the Buffer until they are committed, so they survive network outages.
 */
class BufferReader {
  public:
	/**
	 * @param duplicates_s channel option "duplicates": a value equal to the last one sent is
	 *     skipped unless at least duplicates_s seconds passed. 0 sends all values.
	 * @param allow_equal_timestamps whether a reading with the same timestamp [ms] as the last
	 *     one sent is accepted. Readings with older timestamps are always skipped.
	 */
	BufferReader(Buffer::Ptr buf, const char *name, int duplicates_s, bool allow_equal_timestamps);

	/**
	 * Prepare the next batch: up to max entries that pass the filter. Entries skipped by the
	 * filter are covered by the batch, i.e. acknowledged together with it. If only skipped
	 * entries are pending, they are acknowledged right away and the batch is empty.
	 */
	const std::vector<Buffer::Entry> &next(size_t max);

	/** the batch prepared by the last call to next() */
	const std::vector<Buffer::Entry> &batch() const { return _batch; }

	/** the whole batch was accepted by the middleware */
	void commit();

	/** only the first entry of the batch is done (e.g. rejected by the middleware as
	 * duplicate), the rest is sent again with the next batch */
	void commit_first();

	/** number of entries in the buffer that are not acknowledged yet */
	size_t pending() const { return _buf->size(); }

  private:
	struct Filter {
		int64_t last_ts;   /**< timestamp of the last entry accepted [ms] */
		bool have_last;    /**< last_value is valid (duplicates support) */
		double last_value; /**< value of the last entry accepted (duplicates support) */
	};

	bool accept(Filter &f, const Buffer::Entry &e) const;
	void advance(Buffer::Cursor c, const Filter &f);

	Buffer::Ptr _buf;
	std::string _name;
	int64_t _duplicates_ms;
	bool _allow_equal_timestamps;

	Buffer::Cursor _cursor; /**< first entry not yet acknowledged */
	Filter _filter;         /**< filter state up to _cursor */

	std::vector<Buffer::Entry> _batch;
	Buffer::Cursor _batch_end; /**< cursor after the last entry examined for _batch */
	Filter _batch_filter;      /**< filter state after _batch */
	Buffer::Cursor _first_end; /**< cursor after the first entry of _batch */
	Filter _first_filter;      /**< filter state after the first entry of _batch */
};

} // namespace api
} // namespace vz

#endif /* _BufferReader_hpp_ */
