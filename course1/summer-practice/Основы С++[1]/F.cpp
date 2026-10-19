#include <iostream>
#include <string>
#include <iomanip>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    long long total_int = 0;
    long long total_frac = 0;
    long long MOD = 1000000000000000LL;
    
    string s;
    while (cin >> s) {
        size_t dot = s.find('.');
        long long int_part = stoll(s.substr(0, dot));
        long long frac_part = stoll(s.substr(dot + 1));
        
        total_int += int_part;
        total_frac += frac_part;
    }
    
    total_int += total_frac / MOD;
    total_frac %= MOD;
    
    cout << total_int << "." << setfill('0') << setw(15) << total_frac << "\n";
    
    return 0;
}