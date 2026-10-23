import java.time.YearMonth;
import java.util.Objects;

/**
 * Автоматические проверки классов лабораторной 1 (без внешних библиотек вроде JUnit).
 * Каждая проверка сравнивает то, что вернул метод, с правильным ответом,
 * посчитанным вручную. В конце - итог; если хоть одна проверка не прошла,
 * программа завершается с кодом 1 (так make test и CI узнают об ошибке).
 *
 * Запуск: make test (Linux) или windows\run_tests.bat.
 */
public class Tests {

    private static int passed = 0;   // сколько проверок прошло
    private static int failed = 0;   // сколько не прошло

    public static void main(String[] args) {
        testMoney();
        testFinancialInstrument();
        testBankCard();
        testCreditCard();
        testPolymorphism();

        // итог: все прошли - код 0, иначе 1
        System.out.println();
        if (failed == 0) {
            System.out.println("All tests passed: " + passed + " of " + passed);
        } else {
            System.out.println("Passed " + passed + " of " + (passed + failed) + ", failed " + failed);
            System.exit(1);
        }
    }

    // ===== Класс Money =====

    private static void testMoney() {
        section("Money.format");
        check("0 cents", Money.format(0), "0.00");
        check("5 cents", Money.format(5), "0.05");
        check("100 cents", Money.format(100), "1.00");
        check("15000.50", Money.format(15000_50), "15000.50");
        check("-1.50", Money.format(-150), "-1.50");
        check("-0.05", Money.format(-5), "-0.05");
        check("largest long", Money.format(Long.MAX_VALUE), "92233720368547758.07");
        check("smallest long", Money.format(Long.MIN_VALUE), "-92233720368547758.08");
    }

    // ===== Класс 1: FinancialInstrument =====

    private static void testFinancialInstrument() {
        section("FinancialInstrument");
        FinancialInstrument fi = new FinancialInstrument("  Ivan Ivanov ", " Sberbank", " usd ");
        // конструктор убирает пробелы по краям и переводит валюту в верхний регистр
        check("holder is stripped", fi.getHolderName(), "Ivan Ivanov");
        check("bank is stripped", fi.getIssuingBank(), "Sberbank");
        check("currency in upper case", fi.getCurrency(), "USD");
        check("new instrument is active", fi.isActive(), true);

        // activate/block: true только при смене состояния
        check("activate when active", fi.activate(), false);
        check("block when active", fi.block(), true);
        check("now blocked", fi.isActive(), false);
        check("block when blocked", fi.block(), false);
        check("activate when blocked", fi.activate(), true);
        check("now active", fi.isActive(), true);
        fi.setActive(false);
        check("setActive(false)", fi.isActive(), false);
        fi.setActive(true);

        // текстовые представления
        check("getInfo", fi.getInfo(), "holder: Ivan Ivanov, bank: Sberbank, currency: USD, status: active");
        check("getShortName", fi.getShortName(), "Sberbank/Ivan Ivanov");
        check("toString = getShortName", fi.toString(), "Sberbank/Ivan Ivanov");
        fi.block();
        check("getInfo when blocked", fi.getInfo(),
                "holder: Ivan Ivanov, bank: Sberbank, currency: USD, status: blocked");

        // сеттеры меняют значения
        fi.setHolderName("Petr Petrov");
        fi.setIssuingBank("VTB");
        fi.setCurrency("eur");
        check("setters", fi.getShortName() + " " + fi.getCurrency(), "VTB/Petr Petrov EUR");

        // неправильные значения - исключение, объект не меняется
        checkThrows("null holder", () -> new FinancialInstrument(null, "Bank", "RUB"));
        checkThrows("empty holder", () -> new FinancialInstrument("", "Bank", "RUB"));
        checkThrows("blank holder", () -> new FinancialInstrument("   ", "Bank", "RUB"));
        checkThrows("null bank", () -> new FinancialInstrument("A", null, "RUB"));
        checkThrows("blank bank", () -> new FinancialInstrument("A", " ", "RUB"));
        checkThrows("null currency", () -> new FinancialInstrument("A", "Bank", null));
        checkThrows("currency of 2 letters", () -> new FinancialInstrument("A", "Bank", "RU"));
        checkThrows("currency of 4 letters", () -> new FinancialInstrument("A", "Bank", "RUBL"));
        checkThrows("currency with a digit", () -> new FinancialInstrument("A", "Bank", "R1B"));
        checkThrows("currency in cyrillic", () -> new FinancialInstrument("A", "Bank", "РУБ"));
        checkThrows("setter: blank holder", () -> fi.setHolderName(" "));
        check("value kept after error", fi.getHolderName(), "Petr Petrov");
    }

    // ===== Класс 2: BankCard =====

    private static void testBankCard() {
        section("BankCard: properties");
        BankCard card = newCard(1000_00);
        check("card number", card.getCardNumber(), "2200123456789012");
        check("expiry date", card.getExpiryDate(), "09/35");
        check("balance", card.getBalance(), 1000_00L);
        check("masked number", card.getMaskedNumber(), "2200 **** **** 9012");
        check("short name", card.getShortName(), "T-Bank/2200 **** **** 9012");
        check("getInfo", card.getInfo(),
                "holder: Anna Petrova, bank: T-Bank, currency: RUB, status: active\n"
                + "  card: 2200 **** **** 9012, expires: 09/35, balance: 1000.00 RUB");
        check("zero balance is allowed", newCard(0).getBalance(), 0L);

        // номер карты: только ровно 16 цифр
        checkThrows("number of 15 digits", () -> card.setCardNumber("220012345678901"));
        checkThrows("number of 17 digits", () -> card.setCardNumber("22001234567890123"));
        checkThrows("number with spaces", () -> card.setCardNumber("2200 1234 5678 9012"));
        checkThrows("number with a letter", () -> card.setCardNumber("22001234567890A2"));
        checkThrows("null number", () -> card.setCardNumber(null));
        // срок: MM/YY, месяц 01..12
        checkThrows("month 00", () -> card.setExpiryDate("00/30"));
        checkThrows("month 13", () -> card.setExpiryDate("13/30"));
        checkThrows("one-digit month", () -> card.setExpiryDate("1/30"));
        checkThrows("four-digit year", () -> card.setExpiryDate("01/2030"));
        checkThrows("null expiry date", () -> card.setExpiryDate(null));
        card.setExpiryDate("12/99");
        check("expiry 12/99 is allowed", card.getExpiryDate(), "12/99");
        // баланс не может быть отрицательным
        checkThrows("negative balance", () -> card.setBalance(-1));
        checkThrows("negative balance in constructor", () -> newCard(-1));
        check("values kept after errors", card.getCardNumber() + " " + card.getBalance(), "2200123456789012 100000");

        section("BankCard: isExpired");
        BankCard sep35 = newCard(0);   // срок 09/35
        check("August 2035", sep35.isExpired(YearMonth.of(2035, 8)), false);
        check("September 2035 (last month)", sep35.isExpired(YearMonth.of(2035, 9)), false);
        check("October 2035", sep35.isExpired(YearMonth.of(2035, 10)), true);
        check("January 2036", sep35.isExpired(YearMonth.of(2036, 1)), true);
        check("year 2000 is 00", new BankCard("A", "B", "RUB", "1111222233334444", "01/00", 0)
                .isExpired(YearMonth.of(2000, 2)), true);

        section("BankCard: deposit");
        BankCard d = newCard(1000_00);
        check("deposit result", d.deposit(500_50), OperationResult.OK);
        check("balance after deposit", d.getBalance(), 1500_50L);
        checkThrows("deposit 0", () -> d.deposit(0));
        checkThrows("deposit -1", () -> d.deposit(-1));
        d.block();
        check("deposit to a blocked card", d.deposit(100), OperationResult.CARD_BLOCKED);
        check("balance not changed", d.getBalance(), 1500_50L);
        BankCard big = newCard(Long.MAX_VALUE - 10);
        check("deposit up to the largest long", big.deposit(10), OperationResult.OK);
        checkThrows("deposit over the largest long", () -> big.deposit(1));
        check("balance after overflow attempt", big.getBalance(), Long.MAX_VALUE);

        section("BankCard: pay");
        BankCard p = newCard(1000_00);
        check("pay part", p.pay(300_00), OperationResult.OK);
        check("balance after pay", p.getBalance(), 700_00L);
        check("pay more than balance", p.pay(700_01), OperationResult.NOT_ENOUGH_MONEY);
        check("balance not changed", p.getBalance(), 700_00L);
        check("pay the whole balance", p.pay(700_00), OperationResult.OK);
        check("balance is zero", p.getBalance(), 0L);
        check("pay from zero balance", p.pay(1), OperationResult.NOT_ENOUGH_MONEY);
        checkThrows("pay 0", () -> p.pay(0));
        checkThrows("pay -100", () -> p.pay(-100));
        BankCard blocked = newCard(1000_00);
        blocked.block();
        check("pay with a blocked card", blocked.pay(1), OperationResult.CARD_BLOCKED);
        blocked.activate();
        check("pay after activate", blocked.pay(1), OperationResult.OK);
        BankCard expired = new BankCard("A", "B", "RUB", "1111222233334444", "01/20", 1000_00);
        check("pay with an expired card", expired.pay(1), OperationResult.CARD_EXPIRED);
        check("deposit to an expired card", expired.deposit(1), OperationResult.CARD_EXPIRED);
        check("expired card balance not changed", expired.getBalance(), 1000_00L);
        expired.block();
        check("blocked and expired: blocked first", expired.pay(1), OperationResult.CARD_BLOCKED);
    }

    // ===== Класс 3: CreditCard =====

    private static void testCreditCard() {
        section("CreditCard: properties");
        CreditCard c = newCredit(1000_00, 5000_00, 24.0);
        check("credit limit", c.getCreditLimit(), 5000_00L);
        check("interest rate", c.getInterestRate(), 24.0);
        check("no debt at start", c.getDebt(), 0L);
        check("available credit", c.getAvailableCredit(), 5000_00L);
        check("getInfo", c.getInfo(),
                "holder: Kirill Sidorov, bank: Alfa-Bank, currency: RUB, status: active\n"
                + "  card: 5536 **** **** 5678, expires: 12/36, balance: 1000.00 RUB\n"
                + "  credit: limit 5000.00, debt 0.00, available 5000.00, rate 24.00% per year");
        checkThrows("limit 0", () -> c.setCreditLimit(0));
        checkThrows("negative limit", () -> c.setCreditLimit(-1));
        checkThrows("limit 0 in constructor", () -> newCredit(0, 0, 10));
        checkThrows("rate -0.1", () -> c.setInterestRate(-0.1));
        checkThrows("rate 100.1", () -> c.setInterestRate(100.1));
        checkThrows("rate NaN", () -> c.setInterestRate(Double.NaN));
        checkThrows("rate 150 in constructor", () -> newCredit(0, 100, 150));
        c.setInterestRate(0);
        check("rate 0 is allowed", c.getInterestRate(), 0.0);
        c.setInterestRate(100);
        check("rate 100 is allowed", c.getInterestRate(), 100.0);
        check("values kept after errors", c.getCreditLimit(), 5000_00L);

        section("CreditCard: pay");
        CreditCard p = newCredit(1000_00, 5000_00, 24.0);
        check("pay with own money", p.pay(400_00), OperationResult.OK);
        check("own money spent", p.getBalance() + " " + p.getDebt(), "60000 0");
        check("pay with own money and credit", p.pay(1000_00), OperationResult.OK);
        check("rest taken as debt", p.getBalance() + " " + p.getDebt(), "0 40000");
        check("available credit", p.getAvailableCredit(), 4600_00L);
        check("pay all the credit", p.pay(4600_00), OperationResult.OK);
        check("debt equals limit", p.getDebt() + " " + p.getAvailableCredit(), "500000 0");
        check("pay over the limit", p.pay(1), OperationResult.NOT_ENOUGH_MONEY);
        check("nothing changed", p.getBalance() + " " + p.getDebt(), "0 500000");
        CreditCard q = newCredit(100_00, 1000_00, 24.0);
        check("own + credit is not enough", q.pay(1100_01), OperationResult.NOT_ENOUGH_MONEY);
        check("nothing changed", q.getBalance() + " " + q.getDebt(), "10000 0");
        check("own + credit exactly", q.pay(1100_00), OperationResult.OK);
        check("all money spent", q.getBalance() + " " + q.getDebt(), "0 100000");
        checkThrows("pay 0", () -> q.pay(0));
        CreditCard b = newCredit(0, 1000_00, 24.0);
        b.block();
        check("pay with a blocked credit card", b.pay(1), OperationResult.CARD_BLOCKED);
        check("no debt taken", b.getDebt(), 0L);
        CreditCard huge = newCredit(Long.MAX_VALUE, Long.MAX_VALUE, 10);
        check("huge balance and limit: no overflow", huge.pay(Long.MAX_VALUE), OperationResult.OK);
        check("huge balance spent", huge.getBalance() + " " + huge.getDebt(), "0 0");

        section("CreditCard: limit and debt");
        CreditCard l = newCredit(0, 1000_00, 24.0);
        l.pay(600_00);
        checkThrows("limit below the debt", () -> l.setCreditLimit(599_99));
        l.setCreditLimit(600_00);
        check("limit equal to the debt", l.getAvailableCredit(), 0L);

        section("CreditCard: repayDebt");
        CreditCard r = newCredit(0, 1000_00, 24.0);
        check("repay with no debt", r.repayDebt(100), OperationResult.NO_DEBT);
        check("balance not changed", r.getBalance(), 0L);
        r.pay(500_00);
        check("partial repay", r.repayDebt(200_00), OperationResult.OK);
        check("debt decreased", r.getBalance() + " " + r.getDebt(), "0 30000");
        check("exact repay", r.repayDebt(300_00), OperationResult.OK);
        check("debt closed, balance same", r.getBalance() + " " + r.getDebt(), "0 0");
        r.pay(100_00);
        check("repay with extra money", r.repayDebt(150_00), OperationResult.OK);
        check("extra goes to balance", r.getBalance() + " " + r.getDebt(), "5000 0");
        checkThrows("repay 0", () -> r.repayDebt(0));
        checkThrows("repay -1", () -> r.repayDebt(-1));
        // своих 50.00 + кредит 1000.00 = 1050.00: на копейку больше - отказ, ровно - в долг на весь лимит
        check("pay 1050.01", r.pay(1050_01), OperationResult.NOT_ENOUGH_MONEY);
        check("pay 1050.00", r.pay(1050_00), OperationResult.OK);
        check("whole limit is the debt", r.getBalance() + " " + r.getDebt(), "0 100000");
        r.block();
        check("repay on a blocked card is allowed", r.repayDebt(1000_00), OperationResult.OK);
        check("debt closed on a blocked card", r.getDebt(), 0L);

        section("CreditCard: calculateInterest");
        CreditCard i = newCredit(0, 100000_00, 24.0);
        check("no debt - no interest", i.calculateInterest(12), 0L);
        i.pay(45000_00);
        check("45000.00 at 24% for 3 months", i.calculateInterest(3), 2700_00L);
        check("for 1 month", i.calculateInterest(1), 900_00L);
        check("debt not changed", i.getDebt(), 45000_00L);
        CreditCard round = newCredit(0, 1000_00, 10.0);
        round.pay(1_00);
        check("rounding: 1.00 at 10% for 1 month = 0.0083 -> 0.01", round.calculateInterest(1), 1L);
        round.setInterestRate(0);
        check("rate 0 - no interest", round.calculateInterest(12), 0L);
        checkThrows("0 months", () -> i.calculateInterest(0));
        checkThrows("-1 months", () -> i.calculateInterest(-1));
    }

    // ===== Полиморфизм и наследование =====

    private static void testPolymorphism() {
        section("Polymorphism");
        // переменная типа родителя, объект - подкласса: вызывается метод подкласса
        BankCard asBankCard = newCredit(100_00, 1000_00, 24.0);
        check("CreditCard.pay through a BankCard variable", asBankCard.pay(500_00), OperationResult.OK);
        check("debit card refuses the same pay", newCard(100_00).pay(500_00), OperationResult.NOT_ENOUGH_MONEY);
        FinancialInstrument asInstrument = asBankCard;
        check("getInfo of CreditCard through a FinancialInstrument variable",
                asInstrument.getInfo().split("\n").length, 3);
        check("toString of a card uses BankCard.getShortName", asInstrument.toString(),
                "Alfa-Bank/5536 **** **** 5678");
        // наследование: кредитная карта - это и банковская карта, и финансовый инструмент
        Object obj = asBankCard;
        check("CreditCard is a BankCard", obj instanceof BankCard, true);
        check("CreditCard is a FinancialInstrument", obj instanceof FinancialInstrument, true);
        check("BankCard is not a CreditCard", newCard(0) instanceof CreditCard, false);
        // block/activate унаследованы и работают у кредитной карты
        check("inherited block", asBankCard.block(), true);
        check("blocked credit card refuses", asBankCard.pay(1), OperationResult.CARD_BLOCKED);
    }

    // ===== Вспомогательные функции =====

    // дебетовая карта для проверок, срок 09/35
    private static BankCard newCard(long balance) {
        return new BankCard("Anna Petrova", "T-Bank", "RUB", "2200123456789012", "09/35", balance);
    }

    // кредитная карта для проверок, срок 12/36
    private static CreditCard newCredit(long balance, long limit, double rate) {
        return new CreditCard("Kirill Sidorov", "Alfa-Bank", "RUB", "5536914012345678", "12/36",
                balance, limit, rate);
    }

    // заголовок группы проверок
    private static void section(String name) {
        System.out.println();
        System.out.println("--- " + name + " ---");
    }

    /**
     * Сравнить полученное значение actual с ожидаемым expected и напечатать PASS или FAIL.
     * Тип Object принимает что угодно: строки, числа (long -> Long), enum, boolean.
     * Objects.equals сравнивает содержимое, а не адреса объектов (как == у строк).
     */
    private static void check(String name, Object actual, Object expected) {
        if (Objects.equals(actual, expected)) {
            passed++;
            System.out.println("PASS " + name);
        } else {
            failed++;
            System.out.println("FAIL " + name + ": expected <" + expected + ">, got <" + actual + ">");
        }
    }

    // убедиться, что action бросает IllegalArgumentException
    private static void checkThrows(String name, Runnable action) {
        try {
            action.run();
            failed++;
            System.out.println("FAIL " + name + ": no exception");
        } catch (IllegalArgumentException e) {
            passed++;
            System.out.println("PASS " + name + " (" + e.getMessage() + ")");
        }
    }
}
