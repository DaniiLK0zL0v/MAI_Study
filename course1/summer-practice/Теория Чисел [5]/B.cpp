#include <iostream>
#include <vector>

using namespace std;

void solve() {
    int n;
    cin >> n;
    if (n == 0) {
        cout << 10 << "\n";
        return;
    }
    if (n == 1) {
        cout << 1 << "\n";
        return;
    }
    
    vector<int> digits;
    for (int d = 9; d >= 2; --d) {
        while (n % d == 0) {
            digits.push_back(d);
            n /= d;
        }
    }
    
    if (n > 1) {
        cout << -1 << "\n";
    } else {
        for (int i = digits.size() - 1; i >= 0; --i) {
            cout << digits[i];
        }
        cout << "\n";
    }
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int t;
    if (cin >> t) {
        while (t--) {
            solve();
        }
    }
    return 0;
}
