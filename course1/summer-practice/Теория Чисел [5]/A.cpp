#include <iostream>

using namespace std;

long long gcd(long long a, long long b) {
    while (b != 0) {
        a %= b;
        swap(a, b);
    }
    return a;
}

long long lcm(long long a, long long b) {
    return (a / gcd(a, b)) * b;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        long long ans = 1;
        for (int i = 0; i < n; ++i) {
            long long t;
            cin >> t;
            ans = lcm(ans, t);
        }
        cout << ans << "\n";
    }
    return 0;
}
