
#pragma once
#include <memory>
#include <set>

namespace Core {

    class IObserver {
    public :
        virtual ~IObserver() = default;
        virtual void update () = 0 ;
    };

    class IObservable {
    public :
        virtual  ~IObservable() = default;

        void register_observer(const std::weak_ptr<IObserver>& obs)
        {
            observers_.insert(obs);
        }

         void notify_observers() {
            for (auto it = observers_.begin(); it != observers_.end() ;) {
                if (const auto obs = it->lock()) {
                    obs->update();
                    ++it;
                }
                else {
                    it = observers_.erase(it);
                }
            }
        }

    private :
        std::set<std::weak_ptr<IObserver>, std::owner_less<std::weak_ptr<IObserver>>> observers_;
    };

}
