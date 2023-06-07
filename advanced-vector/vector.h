#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <new>
#include <memory>
#include <type_traits>
#include <utility>

template <typename T>
class RawMemory;

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    /**
     * Конструктор по умолчанию
    */
    Vector() noexcept;
    explicit Vector(size_t size);
    Vector(const Vector& other);
    Vector(Vector&& other) noexcept;

    Vector& operator=(const Vector& other);
    Vector& operator=(Vector&& other) noexcept;

    ~Vector() noexcept;

    void Resize(size_t new_size);
    void Reserve(size_t n);

    template <typename... Types>
    T& EmplaceBack(Types&&... args);
    template <typename ValueType>
    void PushBack(ValueType&& value);

    template <typename... Types>
    iterator Emplace(const_iterator pos, Types&&... args);
    template <typename ValueType>
    iterator Insert(const_iterator pos, ValueType&& value);

    void PopBack() noexcept;
    iterator Erase(const_iterator pos);

    size_t Size() const noexcept;
    size_t Capacity() const noexcept;

    const T& operator[](size_t index) const noexcept;
    T& operator[](size_t index) noexcept;

    iterator begin() noexcept;
    iterator end() noexcept;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
    const_iterator cbegin() const noexcept;
    const_iterator cend() const noexcept;

    void Swap(Vector& other) noexcept;

private:
    RawMemory<T> data_; // Объект управления сырой памятью вектора
    size_t size_ = 0; // Размер вектор

    void MoveElements(T* from, size_t size, T* to);
};

/**
 * Конструктор по умолчанию
*/
template <typename T>
Vector<T>::Vector() noexcept
    : data_()
{}
/**
 * Конструктор, создает вектор заданного размера
*/
template <typename T>
Vector<T>::Vector(size_t size) 
    : data_(size)
    , size_(size)
{
    std::uninitialized_value_construct_n(data_.GetAddress(), size_);
}
/**
 * Конструктор, создает копию передаваемого вектора
*/
template <typename T>
Vector<T>::Vector(const Vector& other) 
    : data_(other.Size())
    , size_(other.Size()) 
{
    std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
}
/**
 * Конструктор перемещения
*/
template <typename T>
Vector<T>::Vector(Vector&& other) noexcept
    : data_(std::move(other.data_))
    , size_(std::move(other.size_))
{
    other.size_ = 0;
}

/**
 * Оператор копирующего присваивания
*/
template <typename T>
Vector<T>& Vector<T>::operator=(const Vector& other) {
    if (this == &other) {
        return *this;
    }

    // Если вместимость вектора меньше размера присваиваемого вектора - 
    // применим идиому copy-and-swap
    if (data_.Capacity() < other.size_) {
        Vector new_vector(other);
        Swap(new_vector);
        return *this;
    }
    // Иначе - копируем элементы из rhs, создаем при необходимости новые
    // или удаляем старые
    else {
        for (size_t i = 0; i < std::min(size_, other.size_); ++i) {
            data_[i] = other[i];
        }
        // Если текущий размер меньше копируемого - создаем новые элементы
        if (size_ < other.size_) {
            std::uninitialized_copy_n(
                other.data_.GetAddress() + size_,
                other.size_ - size_,
                data_.GetAddress() + size_
            );
        }
        // Иначе - удаляем лишние
        else {
            std::destroy_n(data_.GetAddress() + size_, size_ - other.size_);
        }
    }
    // Обновляем размер вектора
    size_ = other.size_;

    return *this;
}
/**
 * Оператор перемещающего присваивания
*/
template <typename T>
Vector<T>& Vector<T>::operator=(Vector&& other) noexcept {
    data_ = std::move(other.data_);
    size_ = std::move(other.size_);
    other.size_ = 0;

    return *this;
}

/**
 * Деструктор, вызывает деструкторы хранящихся в вектор объектов,
 * зарезервированная память автоматически освободится деструктором ~RawMemory()
*/
template <typename T>
Vector<T>::~Vector() noexcept {
    std::destroy_n(data_.GetAddress(), size_);
}

/**
 * Изменяет размер вектора
*/
template <typename T>
void Vector<T>::Resize(size_t new_size) {
    // Если новый размер равен текущему - делать нечего
    if (size_ == new_size) {
        return;
    }

    // Если текущий размер больше нового - удаляем лишние объекты, обновляем размер
    if (size_ > new_size) {
        std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
    }
    // Если меньше - методом Reserve гарантируем достаточное место в векторе 
    // для нового кол-ва элементов, инициилизируем их
    else {
        Reserve(new_size);
        std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
    }
    // Обновляем размер вектора
    size_ = new_size;
}
/**
 * Резервирует памяти под указанное количество элементов вектора
*/
template <typename T>
void Vector<T>::Reserve(size_t new_capacity) {
    if (new_capacity <= data_.Capacity()) {
        return;
    }

    // Аллоцируем новый участок памяти размером new_capacity
    RawMemory<T> new_data(new_capacity);

    MoveElements(data_.GetAddress(), size_, new_data.GetAddress());
    data_.Swap(new_data);
}

/**
 * Передает аргументы конструктору типа T по forwarding-ссылке,
 * полученный элемент добавляется в конец вектора
*/
template <typename T>
template <typename... Types>
T& Vector<T>::EmplaceBack(Types&&... args) {
    // Если вместимость больше размера вектора - создаем новый элемент
    if (size_ < Capacity()) {
        new(data_ + size_) T(std::forward<Types>(args)...);
    }
    // Иначе - переаллоцируем новый участок памяти и вносим элемент туда
    else {
        // Аллоцируем новый участок памяти размером new_capacity
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        
        new(new_data + size_) T(std::forward<Types>(args)...);
        MoveElements(data_.GetAddress(), size_, new_data.GetAddress());
        data_.Swap(new_data);
    }

    ++size_;
    return (*this)[size_ - 1];
}
/**
 * Копирует или перемещает передаваемый элемент в конец вектора 
 * в зависимости от типа объекта, принимая его по forwarding-ссылке
*/
template <typename T>
template <typename ValueType>
void Vector<T>::PushBack(ValueType&& value) {
    EmplaceBack(std::forward<ValueType>(value));
}

/**
 * Копирует или перемещает передаваемый элемент в позицию pos
 * в зависимости от типа объекта, принимая его по forwarding-ссылке
*/
template <typename T>
template <typename ValueType>
typename Vector<T>::iterator Vector<T>::Insert(const_iterator pos, ValueType&& value) {
    return Emplace(pos, std::forward<ValueType>(value));
}
/**
 * Передает аргументы конструктору типа T по forwarding-ссылке,
 * вставляет полученный элемент в позицию pos, возвращает итератор на него
*/
template <typename T>
template <typename... Types>
typename Vector<T>::iterator Vector<T>::Emplace(const_iterator pos, Types&&... args) {
    // Если итератор указывает на конец - вызовем метод EmplaceBack
    if (pos == end()) {
        EmplaceBack(std::forward<Types>(args)...);
    }

    size_t index = pos - begin();
    // Если вместимость больше размера вектора - создаем временный объект, сдвигаем данные
    // вектора после pos на один вправо, и вставляем элемент на место pos
    if (size_ < Capacity()) {
        // Создаем временный объект
        T temp(std::forward<Types>(args)...);

        // Сдвигаем элементы на один вправо
        new(end()) T(std::move((*this)[Size() - 1]));
        std::move_backward(begin() + index, end() - 1, end());

        data_[index] = std::move(temp);
    }
    // Иначе - аллоцируем новую память, вставляем новый элемент на новую позицию,
    // перемещаем элементы на новые места
    else {
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        new(new_data + index) T(std::forward<Types>(args)...);

        // Перемещаем первую половину элементов в диапозоне [begin(), index)
        try {
            MoveElements(data_.GetAddress(), index, new_data.GetAddress());
        }
        catch (...) {
            std::destroy_at(new_data + index);
            throw;
        }

        // Перемещаем вторую половину элементов в диапозоне [size_ - index, end())
        try {
            MoveElements(data_ + index, size_ - index, new_data + (index + 1));
        }
        catch (...) {
            std::destroy_n(data_.GetAddress(), index + 1);
            throw;
        }

        data_.Swap(new_data);
    }

    ++size_;
    return begin() + index;
}

/**
 * Удаляет из вектора последний элемент
*/
template <typename T>
void Vector<T>::PopBack() noexcept {
    assert(size_ > 0);
    std::destroy_at(data_ + (--size_));
}
/**
 * Удаляет вектор из заданной позиции
*/
template <typename T>
typename Vector<T>::iterator Vector<T>::Erase(const_iterator pos) {
    assert(size_ > 0);

    size_t index = pos - begin();
    // Сдвигаем элементы из диапозона [index + 1, end()) на один элемент влево
    std::move(data_ + (index + 1), end(), data_ + index);
    // Вызовем деструктор последнего элемента, обновляем размер вектора
    std::destroy_at(data_ + (--size_));

    return data_ + index;
}

/**
 * Возвращает размер ветора
*/
template <typename T>
size_t Vector<T>::Size() const noexcept {
    return size_;
}
/**
 * Возвращает вместимость вектора
*/
template <typename T>
size_t Vector<T>::Capacity() const noexcept {
    return data_.Capacity();
}

/**
 * Константная ссылка на элемент вектора
*/
template <typename T>
const T& Vector<T>::operator[](size_t index) const noexcept {
    // return const_cast<Vector&>(*this)[index];
    return data_[index];
}
/**
 * Ссылка на элемент вектора
*/
template <typename T>
T& Vector<T>::operator[](size_t index) noexcept {
    // assert(index < size_);
    return data_[index];
}

/**
 * Возвращает итератор на начало вектора
*/
template <typename T>
typename Vector<T>::iterator Vector<T>::begin() noexcept {
    return data_.GetAddress();
}
/**
 * Возвращает итератор на конец вектора
*/
template <typename T>
typename Vector<T>::iterator Vector<T>::end() noexcept {
    return data_ + size_;
}
/**
 * Возвращает константный итератор на начало вектора
*/
template <typename T>
typename Vector<T>::const_iterator Vector<T>::begin() const noexcept {
    return data_.GetAddress();
}
/**
 * Возвращает константный итератор на конец вектора
*/
template <typename T>
typename Vector<T>::const_iterator Vector<T>::end() const noexcept {
    return data_ + size_;
}
/**
 * Возвращает константный итератор на начало вектора
*/
template <typename T>
typename Vector<T>::const_iterator Vector<T>::cbegin() const noexcept {
    return data_.GetAddress();
}
/**
 * Возвращает константный итератор на конец вектора
*/
template <typename T>
typename Vector<T>::const_iterator Vector<T>::cend() const noexcept {
    return data_ + size_;
}

/**
 * Обменивает содержимое векторов
*/
template <typename T>
void Vector<T>::Swap(Vector& other) noexcept {
    data_.Swap(other.data_);
    std::swap(size_, other.size_);
}

/**
 * Безопасно перемещает или копирует n элементов из одного класса-обертки в другой,
 * очищает содержимое from
*/
template <typename T>
void Vector<T>::MoveElements(T* from, size_t size, T* to) {
    // Если объект типа T имеет noexcept move-конструктор или не имеет конструктора копирования - 
    // перемещаем объекты из data_ в new_data, в противном случае копируем их
    if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
        std::uninitialized_move_n(from, size, to);
    }
    else {
        std::uninitialized_copy_n(from, size, to);
    }
    // Освобождаем старую память
    std::destroy_n(from, size);
}

/**
 * Класс-обертка для управления сырой памятью
*/
template <typename T>
class RawMemory {
public:
    RawMemory() = default;
    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory& other) = delete;
    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::move(other.buffer_))
        , capacity_(other.capacity_)
    {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }

    RawMemory& operator=(const RawMemory& other) = delete;
    RawMemory& operator=(RawMemory&& other) {
        buffer_ = std::move(other.buffer_);
        capacity_ = std::move(other.capacity_);

        other.buffer_ = nullptr;
        other.capacity_ = 0;

        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }
    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }
    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    /**
     * Выделяет сырую память под n элементов и возвращает указатель на неё
    */
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }
    /**
     * Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    */
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};