#pragma once
#include <cstddef>
#include "kit/counters.h"

namespace team{
    template <typename T>
    class Vector{
        public:

            //returns number of items in the array
            std::size_t size() const{
                return count;
            }

            //returns the capacity of the array
            std::size_t capacity() const{
                return cap;
            }

            //checks if array is empty
            bool empty() const{
                return count == 0;
            }

            //clears array
            void clear(){
                count = 0;
            }

            //removes last element 
            void pop_back(){
                if(count > 0){
                    count--;
                }
            }

            //returns specific element at given index
            T& operator[](std::size_t index){
                return data[index];
            }

            //read version of index
            const T& operator[](std::size_t index) const{
                return data[index];
            }

            //returns last element
            T& back(){
                return data[count - 1];
            }

            //reads the counters
            const kit::Counters& stats() const{
                return counters;
            }

            //resets counters
            void reset_stats(){
                counters.reset();   
            }

            //checks to make sure capacity isn't exceeded
            bool validate() const{
                return count <= cap;
            }

            //bytes used
            std::size_t memory_bytes() const{
                return sizeof(*this) +sizeof(T) * cap;
            }

            //adds element to end of the array, reallocates size if needed
            void push_back(const T& value){
                if(count >= cap){
                    reallocate();
                }
                data[count] = value;
                count++;
            }
            //constructor
            Vector(){
                count = 0;
                cap = 1;
                data = new T[cap];
                KIT_COUNT(counters, allocations);
            }

            //destructor
            ~Vector(){
                delete[] data;
            }

            //copy constructor
            Vector(const Vector& other){
                count = other.count;
                cap = other.cap;
                data = new T[cap];
                KIT_COUNT(counters, allocations);
                for(std::size_t i = 0; i < count; i++){
                    data[i] = other.data[i];
                    KIT_COUNT(counters, moves);
                }
            }

            //assignment operator
            Vector & operator=(const Vector& other){
                if(this != &other){
                    delete[] data;
                    count = other.count;
                    cap = other.cap;
                    data = new T[cap];
                    for(std::size_t i = 0; i < count; i++){
                        data[i] = other.data[i];
                    }
                }
                return *this;
            }

            private:
                T* data = nullptr;
                std::size_t count = 0;
                std::size_t cap = 0;
                kit::Counters counters;

                //reallocates the size by doubling the array
                    void reallocate() {
        cap *= 2;
        T* new_data = new T[cap];
        KIT_COUNT(counters, allocations);               // fix 8
        for (std::size_t i = 0; i < count; i++) {
            new_data[i] = data[i];
            KIT_COUNT(counters, moves);
        }
        delete[] data;
        data = new_data;
        KIT_COUNT_EXTRA(counters, "resizes");
    }
};    

}
