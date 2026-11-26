#pragma once

#include <cstddef>
#include <vector>

#include "kit/counters.h"

namespace team{
    template <typename T>
    class StlVector{
        public:
            

            void push_back(const T& value){
               items.push_back(value);
            }

            void pop_back(){
               items.pop_back();
            }

            T& operator[](std::size_t index){
                return items[index];
            }

            //read only version 
            const T& operator[](std::size_t index) const{
                return items[index];
            }

            T& back(){
                return items.back();
            }

            std::size_t size() const{
                return items.size();
            }

            bool empty() const{
                return items.empty();       
            }

            void clear(){
                items.clear();
            }   

            const kit::Counters& stats() const{
                return counters;
            }

            void reset_stat(){
                counters.reset();
            }
        private:
            std::vector<T> items;  
            kit::Counters counters;
         
    };
}