#include <iostream>

using namespace std;

long long gcd(long long a, long long b) {
    while (b != 0) {
        a %= b;
        swap(a, b);
    }
    return a;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        for (int i = 0; i < n; ++i) {
            long long x1, y1, x2, y2;
            cin >> x1 >> y1 >> x2 >> y2;
            
            long long dx = x2 - x1;
            if (dx < 0) dx = -dx;
            
            long long dy = y2 - y1;
            if (dy < 0) dy = -dy;
            
            cout << gcd(dx, dy) + 1 << "\n";
        }
    }
    return 0;
}
