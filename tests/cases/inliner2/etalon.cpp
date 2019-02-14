#include <myvector>

namespace spcppl {

    template <typename T>
    double f(T& t) {
        return double(t);
    }

} // namespace spcppl

class X {
public:
    void f() const {
    }
};

template <typename T>
void goF(const T& t) {
    t.f();
}

class Vertex {
public:
    Vertex() {}
    
};

struct Y {
    bool operator ++ (int x) {
        return true;
    }
    
};

bool operator + (Y a, Y b) {
    return true;
}

struct Used {
    int x;

    ~Used() {
        x = 2;
    }
};


struct UsedTypedefedStruct {
    
};

typedef UsedTypedefedStruct UsedTypedef;


struct UsedTypedefedStruct2 {
    
};

typedef UsedTypedefedStruct2 UsedTypedef2;
typedef UsedTypedef2 RecursiveUsedTypedef2;


struct UsedCall {
    static void staticCall() {}
};

template<typename T>
struct V {
    T t;
};

template<typename T>
struct V2 {
    
};


struct S1 {
    typedef int type;
};


template<typename T>
struct S3 {
    typedef T type;
    static type get() {
        return type();
    }
};

struct S4 {
    typedef V2<int> type;
};


template<typename T>
struct S6 {
};

struct S7 {};

template<int n>
struct IntTemplate {
    static const int val = n*n;
};

typedef long long ll;


int main() {
    {
        int x;
        spcppl::f(x);
        goF(X());
    }

    {
        mystd::vector<Vertex> v;
        v.resize(100);
    }

    {
        Y y;
        y++;
        y + y;
    }

    {
        Used used;
    }

    {
        UsedTypedef f;
        RecursiveUsedTypedef2 g;
    }

    {
        mystd::vector<UsedCall>::callIntoTemplate();
    }

    {
        S1::type i;
        auto v = S3<V<int> >::get();
        S4::type v2;
    }

    {
        S6<ll> s6;
        S6<S7> s6_s7;
    }

    {
        IntTemplate<4>::val;
    }

    {
        typedef int ta;
        
        ta i = 2;
        struct LocalUsed {
            
            void used() {}
            
        } lu;
        lu.used();
        
    }

    {
        struct Outer {
            struct Inner {};
        };
        Outer::Inner inner;
    }

    return 0;
}


