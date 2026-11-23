#include "game/phase9/annual-league-config.h"
#include <iostream>

int main() {
    namespace a=mvp::annual_schedule;
    const auto teams=a::authored::Teams();
    for(unsigned year:{2028u,2029u,2032u,2100u}) {
        const auto season=a::authored::Generate(year);
        if(season.fixtures.size()!=8588||season.conferences.size()!=30)throw std::runtime_error("annual population");
        std::vector<unsigned> games(309),home(309);
        std::set<std::tuple<unsigned,int32_t,unsigned>> times;
        for(const auto& fixture:season.fixtures){
            if(fixture.home==fixture.away||fixture.home>=309||fixture.away>=309)throw std::runtime_error("annual participants");
            for(auto team:{fixture.home,fixture.away}){
                ++games[team];
                if(!times.emplace(team,fixture.day,fixture.minute).second)throw std::runtime_error("annual simultaneous games");
                if(teams[team].id=="ncaa:77"&&a::Weekday(fixture.day)==6)throw std::runtime_error("BYU Sunday");
            }
            ++home[fixture.home];
        }
        for(unsigned i=0;i<309;++i)if(games[i]!=teams[i].budget||home[i]*2!=games[i])throw std::runtime_error("annual budgets/home balance");
        std::cout<<"PASS "<<year<<": 8588 games, 309 schools, 30 conferences, exact budgets/home balance, BYU Sunday and duplicate-time checks; attempt "<<season.attempt<<'\n';
    }
}
