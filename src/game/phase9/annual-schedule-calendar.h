#pragma once
#include <cstdint>
#include <stdexcept>

// Pure date policy for the annual generator. Guest state is untouched here.
// Published first-season dates override the recurring authored weekday rule.
namespace mvp::annual_schedule {
struct EventRule {
    uint32_t key;
    int32_t span, start_weekday;
    int32_t published_year, published_start, published_end;
    bool ivy;
};
struct Window {int32_t first, last, event_start, event_end, deadline;};
inline int32_t JanuaryFirst(int32_t year) {
    if (year < 2027 || year > 9999) throw std::invalid_argument("Annual schedule year out of range");
    const int64_t prior = int64_t(year)-1;
    return int32_t(29221+365*(int64_t(year)-2028)+
        prior/4-prior/100+prior/400-(2027/4-2027/100+2027/400));
}
inline bool Leap(int32_t year) {return year%4==0 && (year%100!=0 || year%400==0);}
inline int32_t Day(int32_t year, int32_t month, int32_t day) {
    static constexpr int32_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month<1 || month>12 || day<1 || day>days[month-1]+(month==2 && Leap(year)))
        throw std::invalid_argument("Invalid annual calendar date");
    auto result=JanuaryFirst(year)+day-1;
    for (int i=1;i<month;++i) result+=days[i-1]+(i==2 && Leap(year));
    return result;
}
inline int32_t Weekday(int32_t day) {return ((day-29221+5)%7+7)%7;}
inline int32_t OnOrAfter(int32_t day,int32_t weekday) {return day+(weekday-Weekday(day)+7)%7;}
inline int32_t OnOrBefore(int32_t day,int32_t weekday) {return day-(Weekday(day)-weekday+7)%7;}
inline Window SeasonWindow(int32_t year,const EventRule& rule,int32_t published_first=0) {
    if (year>9998 || rule.span<1 || rule.span>31 || rule.start_weekday<0 || rule.start_weekday>6)
        throw std::invalid_argument("Invalid annual event policy");
    Window w{};
    w.first=published_first ? published_first : OnOrAfter(Day(year,2,15),4);
    w.deadline=OnOrBefore(Day(year,5,31),0);
    if (rule.published_year==year) {
        w.event_start=rule.published_start; w.event_end=rule.published_end;
    } else {
        w.event_start=OnOrBefore(w.deadline-rule.span,rule.start_weekday);
        w.event_end=w.event_start+rule.span-1;
    }
    w.last=OnOrBefore(w.event_start-1,6);
    if (rule.ivy) {
        w.last=OnOrAfter(Day(year,5,1),6);
        w.event_start=w.last+12; w.event_end=w.event_start+3;
    }
    if (!(JanuaryFirst(year)<=w.first && w.first<=w.last && w.last<w.event_start &&
          w.event_start<=w.event_end && w.event_end<w.deadline && w.deadline<JanuaryFirst(year+1)))
        throw std::invalid_argument("Annual event window cannot satisfy season bounds");
    return w;
}
}
