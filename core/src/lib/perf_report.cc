/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2023-2023 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/

#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cstring>

#include "lib/perf_report.h"
#include "include/messages.h"

using ns = std::chrono::nanoseconds;

struct SplitDuration {
  std::chrono::hours h;
  std::chrono::minutes m;
  std::chrono::seconds s;
  std::chrono::milliseconds ms;
  std::chrono::microseconds us;
  std::chrono::nanoseconds ns;

  template <typename Duration> SplitDuration(Duration d)
  {
    h = std::chrono::duration_cast<std::chrono::hours>(d);
    d -= h;
    m = std::chrono::duration_cast<std::chrono::minutes>(d);
    d -= m;
    s = std::chrono::duration_cast<std::chrono::seconds>(d);
    d -= s;
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
    d -= ms;
    us = std::chrono::duration_cast<std::chrono::microseconds>(d);
    d -= us;
    ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
  }

  int64_t hours() { return h.count(); }
  int64_t minutes() { return m.count(); }
  int64_t seconds() { return s.count(); }
  int64_t millis() { return ms.count(); }
  int64_t micros() { return us.count(); }
  int64_t nanos() { return ns.count(); }

  friend std::ostream& operator<<(std::ostream& out, SplitDuration d)
  {
    return out << std::setfill('0') << std::setw(2) << d.hours() << ":"
               << std::setw(2) << d.minutes() << ":" << std::setw(2)
               << d.seconds() << "." << std::setw(3) << d.millis() << "-"
               << std::setw(3) << d.micros() << std::setfill(' ');
  }
};

static auto max_child_values(const ThreadCallstackReport::Node* node)
{
  struct {
    std::size_t name_length;
    std::size_t depth;
  } max;
  max.depth = node->depth();
  max.name_length = 0;
  auto& children = node->children_view();
  // this looks weird but is necessary for return type deduction
  // since we want to call this function recursively
  if (children.size() == 0) return max;
  for (auto& [source, child] : children) {
    auto child_max = max_child_values(child.get());
    auto child_max_name
        = std::max(std::strlen(source->c_str()), child_max.name_length);
    max.name_length = std::max(max.name_length, child_max_name);
    max.depth = std::max(max.depth, child_max.depth);
  }
  return max;
}

static void PrintNode(std::ostringstream& out,
		      bool relative,
                      const char* name,
                      std::size_t depth,
                      std::chrono::nanoseconds parentns,
                      std::size_t max_name_length,
                      std::size_t max_depth,
                      const ThreadCallstackReport::Node* node)
{
  // depth is (modulo a shared offset) equal to current->depth
  std::size_t offset
      = (max_name_length - std::strlen(name)) + (max_depth - depth);
  SplitDuration d(node->time_spent());
  out << std::setw(depth) << "" << name << ": "
      << std::setw(offset) << std::setfill('-') << ((offset>0) ? " " : "")
      << std::setfill(' ') << d;
  if (parentns.count() != 0) {
    out << " (" << std::setw(6) << std::fixed << std::setprecision(2)
        << double(node->time_spent().count() * 100) / double(parentns.count())
        << "%)";
  }
  out << "\n";

  if (depth < max_depth) {
    std::vector<
      std::pair<BlockIdentity const*, ThreadCallstackReport::Node const*>>
      children;
    auto& view = node->children_view();
    children.reserve(view.size());
    for (auto& [source, child] : view) {
      children.emplace_back(source, child.get());
    }

    std::sort(children.begin(), children.end(), [](auto& p1, auto& p2) {
      auto t1 = p1.second->time_spent();
      auto t2 = p2.second->time_spent();
      if (t1 > t2) { return true; }
      if ((t1 == t2) && (p1.first > p2.first)) { return true; }
      return false;
    });
    if (relative) {
      parentns = node->time_spent();
    }
    for (auto& [id, child] : children) {
      PrintNode(out, relative, id->c_str(), depth + 1, parentns, max_name_length,
		max_depth, child);
    }
  }
}

static std::int64_t PrintCollapsedNode(std::ostringstream& out,
                                       std::string path,
				       std::size_t max_depth,
                                       const ThreadCallstackReport::Node* node)
{
  std::int64_t child_time = 0;
  if (node->depth() < max_depth) {
    auto& view = node->children_view();

    for (auto& [id, child] : view) {
      std::string copy = path;
      copy += ";";
      copy += id->c_str();
      child_time += PrintCollapsedNode(out, copy, max_depth, child.get());
    }

    ASSERT(child_time <= node->time_spent().count());
  }
  out << path << " " << node->time_spent().count() - child_time << "\n";
  return node->time_spent().count();
}

static ns CreateOverview(std::unordered_map<const BlockIdentity*, ns>& time_spent,
			 const BlockIdentity* node_id,
			 const ThreadCallstackReport::Node* node,
			 bool relative) {
  ns time_inside_node = node->time_spent();

  ns child_time{0};
  for (auto& [id, child] : node->children_view()) {
    child_time += CreateOverview(time_spent, id, child.get(), relative);
  }

  ns attributed_time = time_inside_node;
  if (relative) {
    attributed_time -= child_time;
  }

  time_spent[node_id] += attributed_time;
  return time_inside_node;
}

void ThreadOverviewReport::begin_report(event::time_point current)
{
  now = current;
}

void ThreadOverviewReport::begin_event(event::OpenEvent e)
{
  stack.push_back(e);
}

void ThreadOverviewReport::end_event(event::CloseEvent e)
{
  using namespace std::chrono;

  ASSERT(stack.size() > 0);
  ASSERT(stack.back().source == e.source);

  auto start = stack.back().start;
  auto end = e.end;

  cul_time[e.source] += duration_cast<nanoseconds>(end - start);

  stack.pop_back();
}

std::unordered_map<BlockIdentity const*, std::chrono::nanoseconds>
ThreadOverviewReport::as_of(event::time_point tp) const
{
  using namespace std::chrono;
  std::unordered_map<BlockIdentity const*, std::chrono::nanoseconds> result;

  result = cul_time;  // copy
  for (auto& open : stack) {
    if (open.start > tp) continue;
    result[open.source] += duration_cast<nanoseconds>(tp - open.start);
  }

  return result;
}

std::string OverviewReport::str(std::size_t NumToShow) const
{
  using namespace std::chrono;
  std::ostringstream report{};
  event::time_point now = event::clock::now();
  report << "=== Start Performance Report (Overview) ===\n";
  std::unique_lock lock{threads_mut};
  for (auto& [id, reporter] : threads) {
    report << "== Thread: " << id << " ==\n";
    auto data = reporter.lock()->as_of(now);

    std::vector<std::pair<BlockIdentity const*, nanoseconds>> entries(
        data.begin(), data.end());
    std::sort(entries.begin(), entries.end(), [](auto& p1, auto& p2) {
      if (p1.second > p2.second) { return true; }
      if ((p1.second == p2.second) && (p1.first > p2.first)) { return true; }
      return false;
    });

    if (NumToShow < entries.size()) { entries.resize(NumToShow); }

    std::size_t maxwidth = 0;
    for (auto [id, _] : entries) {
      maxwidth = std::max(std::strlen(id->c_str()), maxwidth);
    }

    auto max_time = duration_cast<nanoseconds>(now - start);
    for (auto [id, time] : entries) {
      SplitDuration d(time);
      // TODO(C++20): replace this with std::format
      report << std::setw(maxwidth) << id->c_str() << ": "
             << d
             // XXX.XX = 6 chars
             << " (" << std::setw(6) << std::fixed << std::setprecision(2)
             << double(time.count() * 100) / double(max_time.count()) << "%)"
             << "\n";
    }
  }
  report << "=== End Performance Report ===\n";
  return report.str();
}

OverviewReport::~OverviewReport() { Dmsg1(500, "%s", str(ShowAll).c_str()); }

std::string CallstackReport::callstack_str(std::size_t max_depth, bool relative) const
{
  using namespace std::chrono;
  event::time_point now = event::clock::now();
  std::ostringstream report{};
  report << "=== Start Performance Report (Callstack) ===\n";
  std::shared_lock lock{threads_mut};
  for (auto& [id, thread] : threads) {
    report << "== Thread: " << id << " ==\n";
    auto node = thread.as_of(now);
    auto max_values = max_child_values(node.get());

    auto max_print_depth = std::min(static_cast<std::size_t>(max_depth),
				    max_values.depth);

    std::string_view base_name = "Measured";
    PrintNode(report, relative, base_name.data(), 0, duration_cast<nanoseconds>(now - start),
              std::max(base_name.size(), max_values.name_length),
              max_print_depth, node.get());
  }
  report << "=== End Performance Report ===\n";
  return report.str();
}

std::string CallstackReport::collapsed_str(std::size_t max_depth) const
{
  using namespace std::chrono;
  event::time_point now = event::clock::now();
  std::ostringstream report{};
  report << "=== Start Performance Report (Collapsed Callstack) ===\n";
  std::shared_lock lock{threads_mut};
  for (auto& [id, thread] : threads) {
    report << "== Thread: " << id << " ==\n";
    auto node = thread.as_of(now);
    PrintCollapsedNode(report, "Measured", max_depth, node.get());
  }
  report << "=== End Performance Report ===\n";
  return report.str();
}

std::string CallstackReport::overview_str(std::size_t show_top_n,
					  bool relative) const
{
  using namespace std::chrono;
  event::time_point now = event::clock::now();
  std::ostringstream report{};
  report << "=== Start Performance Report (Overview) ===\n";
  std::shared_lock lock{threads_mut};
  BlockIdentity top{"Measured"};
  for (auto& [id, thread] : threads) {
    report << "== Thread: " << id << " ==\n";
    auto node = thread.as_of(now);
    std::unordered_map<const BlockIdentity*, ns> time_spent;
    CreateOverview(time_spent, &top, node.get(), relative);

    std::vector<
      std::pair<const BlockIdentity*, ns>>
      blocks{time_spent.begin(), time_spent.end()};

    std::sort(blocks.begin(), blocks.end(), [](auto& p1, auto& p2) {
      auto t1 = p1.second.count();
      auto t2 = p2.second.count();
      if (t1 > t2) { return true; }
      if ((t1 == t2) && (p1.first > p2.first)) { return true; }
      return false;
    });

    if (show_top_n < blocks.size()) {
      blocks.resize(show_top_n);
    }

    std::size_t maxwidth = 0;
    for (auto [id, _] : blocks) {
      maxwidth = std::max(std::strlen(id->c_str()), maxwidth);
    }
    auto max_time = duration_cast<nanoseconds>(now - start);

    for (auto [id, time] : blocks) {
      SplitDuration d{time};
      report << std::setw(maxwidth) << id->c_str() << ": "
             << d
             // XXX.XX = 6 chars
             << " (" << std::setw(6) << std::fixed << std::setprecision(2)
             << double(time.count() * 100) / double(max_time.count()) << "%)"
             << "\n";
    }
  }
  report << "=== End Performance Report ===\n";
  return report.str();
}

CallstackReport::~CallstackReport() {
  Dmsg1(500, "%s", callstack_str().c_str());
}
