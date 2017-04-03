/**
 *    > Author:            UncP
 *    > Mail:         770778010@qq.com
 *    > Github:    https://www.github.com/UncP/Mushroom
 *    > Created Time:  2016-10-22 09:10:01
**/

#include <sstream>

#include "page.hpp"

namespace Mushroom {

uint32_t Page::PageSize;

void Page::SetPageInfo(uint32_t page_size)
{
	PageSize = page_size;
}

uint16_t Page::CalculateDegree(uint8_t key_len, uint8_t pre_len)
{
	Page *page = 0;
	uint16_t offset = (char *)page->data_ - (char *)page + pre_len;
	return (PageSize - offset) / (PageByte + IndexByte + key_len);
}

void Page::Initialize(page_id page_no, Type type, uint8_t key_len, uint8_t level,
	uint16_t degree)
{
	memset(this, 0, PageSize);
	page_no_ = page_no;
	degree_  = degree;
	type_    = (uint8_t)type;
	key_len_ = key_len;
	level_   = level;
}

bool Page::Traverse(const KeySlice *key, uint16_t *idx, KeySlice **slice, int type) const
{
	uint16_t low = 0, high = total_key_, mid = 0;
	uint16_t *index = Index();
	if (pre_len_) {
		int res = ComparePrefix(key, data_, pre_len_);
		if (res < 0) {
			*idx = 0;
			return false;
		} else if (res > 0) {
			*idx = high--;
			*slice = Key(index, high);
			return false;
		}
	}
	KeySlice *curr = 0;
	while (low != high) {
		mid = low + ((high - low) >> 1);
		curr = Key(index, mid);
		int res = CompareSuffix(key, curr, pre_len_, key_len_);
		if (res < 0) {
			high = mid;
		} else if (res > 0) {
			low = mid + 1;
		} else {
			if (type) {
				*idx = mid;
				return true;
			} else {
				low = mid + 1;
			}
		}
	}
	*idx = high;
	if (high) *slice = Key(index, high-1);
	return false;
}

page_id Page::Descend(const KeySlice *key) const
{
	uint16_t index;
	KeySlice *slice = 0;
	Traverse(key, &index, &slice, 0);
	return index ? slice->PageNo() : first_;
}

bool Page::Search(KeySlice *key, uint16_t *index) const
{
	KeySlice *slice = 0;
	return Traverse(key, index, &slice);
}

InsertStatus Page::Insert(const KeySlice *key, page_id &page_no)
{
	uint16_t pos;
	KeySlice *slice = 0;
	bool flag = Traverse(key, &pos, &slice);
	if (flag) return ExistedKey;
	if (pos == total_key_ && pos) {
		page_no = Next();
		assert(page_no);
		return MoveRight;
	}

	uint16_t end = total_key_ * (PageByte + key_len_) + pre_len_;
	page_id num = key->PageNo();
	memcpy(data_ + end, &num, PageByte);
	memcpy(data_ + end + PageByte, key->Data() + pre_len_, key_len_);

	uint16_t *index = Index();
	--index;
	if (pos) memmove(&index[0], &index[1], pos << 1);
	index[pos] = end;
	++total_key_;
	return InsertOk;
}

bool Page::Ascend(KeySlice *key, page_id *page_no, uint16_t *idx)
{
	uint16_t *index = Index();
	if (*idx < (total_key_ - 1)) {
		if (pre_len_)
			CopyPrefix(key, data_, pre_len_);
		CopyKey(key, Key(index, *idx), pre_len_, key_len_);
		++*idx;
		return true;
	} else {
		*page_no = Key(index, *idx)->PageNo();
		*idx = 0;
		return false;
	}
}

void Page::Split(Page *that, KeySlice *slice)
{
	uint16_t left = total_key_ >> 1, right = total_key_ - left, index = left;
	uint16_t *l_idx = this->Index();
	uint16_t *r_idx = that->Index();
	KeySlice *fence = Key(l_idx, left++);

	if (pre_len_) {
		memcpy(that->data_, this->data_, pre_len_);
		that->pre_len_ = this->pre_len_;
		CopyPrefix(slice, data_, pre_len_);
	}

	slice->AssignPageNo(that->page_no_);
	memcpy(slice->Data() + pre_len_, fence->Data(), key_len_);

	if (level_) {
		that->AssignFirst(fence->PageNo());
		memcpy(fence->Data(), Key(l_idx, left)->Data(), key_len_);
		r_idx -= --right;
		++index;
	} else {
		r_idx -= right;
	}

	fence->AssignPageNo(that->page_no_);

	uint16_t slot_len = PageByte + key_len_;
	for (uint16_t i = index, j = 0; i != total_key_; ++i, ++j) {
		r_idx[j] = that->pre_len_ + j * slot_len;
		KeySlice *l = this->Key(l_idx, i);
		KeySlice *r = that->Key(r_idx, j);
		CopyKey(r, l, 0, key_len_);
	}
	uint16_t limit = left * slot_len + pre_len_, j = 0;
	for (uint16_t i = left; i < total_key_ && j < left; ++i) {
		if (l_idx[i] < limit) {
			for (; j < left; ++j) {
				if (l_idx[j] >= limit) {
					KeySlice *o = this->Key(l_idx, i);
					KeySlice *n = this->Key(l_idx, j);
					l_idx[j] = l_idx[i];
					CopyKey(o, n, 0, key_len_);
					++j;
					break;
				}
			}
		}
	}

	uint16_t offset = total_key_ - left;
	memmove(&l_idx[offset], &l_idx[0], left << 1);

	this->total_key_ = left;
	that->total_key_ = right;
}

bool Page::NeedSplit()
{
	if (total_key_ < degree_) return false;
	uint16_t *index = Index();
	const char *first = Key(index, 0)->Data();
	const char *last  = Key(index, total_key_ - 1)->Data();
	char prefix[key_len_];
	uint8_t pre_len = 0;
	for (; first[pre_len] == last[pre_len]; ++pre_len)
		prefix[pre_len] = first[pre_len];
	if (!pre_len)
		return true;
	uint16_t degree = CalculateDegree(key_len_ - pre_len, pre_len + pre_len_);
	if (degree <= degree_)
		return true;
	char buf[PageSize];
	Page *copy = (Page *)buf;
	memcpy(copy, this, PageSize);
	memcpy(data_ + pre_len_, prefix, pre_len);
	char *curr = data_ + pre_len_ + pre_len;
	uint16_t *cindex = copy->Index();
	uint16_t suf_len = key_len_ - pre_len;
	for (uint16_t i = 0; i != total_key_; ++i, ++index) {
		KeySlice *key = copy->Key(cindex, i);
		*index = curr - data_;
		page_id page_no = key->PageNo();
		memcpy(curr, &page_no, PageByte);
		curr += PageByte;
		memcpy(curr, key->Data() + pre_len, suf_len);
		curr += suf_len;
	}
	pre_len_ += pre_len;
	key_len_ -= pre_len;
	degree_  = degree;
	return false;
}

std::string Page::ToString() const
{
	std::ostringstream os;
	os << "type: ";
	if (type_ == LEAF)   os << "leaf  ";
	if (type_ == BRANCH) os << "branch  ";
	if (type_ == ROOT)   os << "root  ";
	os << "page_no: " << page_no_ << " ";
	os << "first: " << first_ << " ";
	os << "tot_key: " << total_key_ << " ";
	os << "level: " << (int)level_ << " ";
	os << "key_len: " << (int)key_len_ << " ";

	if (pre_len_) {
		os << "pre_len: " << (int)pre_len_ << " ";
		os << "prefix: " << std::string(data_, pre_len_) << "\n";
	}

	assert(total_key_ < 255);
	uint16_t *index = Index();
	for (uint16_t i = 0; i != total_key_; ++i)
		os << index[i] << " ";
	os << "\n";
	for (uint16_t i = 0; i != total_key_; ++i) {
		KeySlice *key = Key(index, i);
		os << key->ToString(key_len_);
	}
	os << "\nnext: " << Key(index, total_key_-1)->PageNo() << "\n";
	return os.str();
}

} // namespace Mushroom
