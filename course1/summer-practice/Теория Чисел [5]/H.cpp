#include <iostream>
#include <vector>

using namespace std;

const int MAXN = 40000000;
vector<uint8_t> cnt(MAXN + 1, 0);
vector<bool> is_prime(MAXN + 1, true);

void precompute() {
    is_prime[0] = is_prime[1] = false;
    for (int i = 2; i <= MAXN; ++i) {
        if (is_prime[i]) {
            for (int j = i; j <= MAXN; j += i) {
                cnt[j]++;
                if (j > i) {
                    is_prime[j] = false;
                }
            }
        }
    }
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    precompute();
    
    int t;
    if (cin >> t) {
        while (t--) {
            int n;
            cin >> n;
            if (is_prime[n]) {
                cout << "Prime\n";
            } else {
                cout << (int)cnt[n] << "\n";
            }
        }
    }
    return 0;
}
