#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

struct Element {
    long long val;
    int idx;
    bool operator<(const Element& other) const {
        if (val != other.val) {
            return val < other.val;
        }
        return idx < other.idx;
    }
};

void solve() {
    int n;
    if (cin >> n) {
        vector<long long> a(n);
        vector<Element> elements(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
            elements[i] = {a[i], i};
        }
        
        sort(elements.begin(), elements.end());
        
        vector<int> prev_occ(n, -1);
        for (int i = 1; i < n; ++i) {
            if (elements[i].val == elements[i - 1].val) {
                prev_occ[elements[i].idx] = elements[i - 1].idx;
            }
        }
        
        int max_len = 0;
        int L = 0;
        for (int R = 0; R < n; ++R) {
            if (prev_occ[R] >= L) {
                L = prev_occ[R] + 1;
            }
            int cur_len = R - L + 1;
            if (cur_len > max_len) {
                max_len = cur_len;
            }
        }
        cout << max_len << "\n";
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
