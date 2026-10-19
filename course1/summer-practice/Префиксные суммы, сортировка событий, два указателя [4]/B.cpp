#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> a(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
        }
        
        vector<int> L(n);
        vector<int> st;
        for (int i = 0; i < n; ++i) {
            while (!st.empty() && a[st.back()] <= a[i]) {
                st.pop_back();
            }
            L[i] = st.empty() ? -1 : st.back();
            st.push_back(i);
        }
        
        vector<int> R(n);
        st.clear();
        for (int i = n - 1; i >= 0; --i) {
            while (!st.empty() && a[st.back()] < a[i]) {
                st.pop_back();
            }
            R[i] = st.empty() ? n : st.back();
            st.push_back(i);
        }
        
        long long total_sum = 0;
        for (int i = 0; i < n; ++i) {
            long long subarrays = (long long)(i - L[i]) * (R[i] - i);
            total_sum += subarrays * a[i];
        }
        
        cout << total_sum << "\n";
    }
    return 0;
}
