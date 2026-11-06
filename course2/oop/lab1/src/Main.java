/**
 * Лабораторная работа 1 по ООП, вариант 11 (банковская карта).
 * Демонстрация иерархии FinancialInstrument -> BankCard -> CreditCard:
 * методы каждого класса, полиморфизм и проверки неправильных значений.
 *
 * Классы карт сами ничего не печатают: их методы возвращают результат,
 * а печатает его этот класс. Суммы - в копейках: 15000_00 = 15000.00 руб.
 */
public class Main {

    public static void main(String[] args) {
        printTitle("OOP lab 1, variant 11: bank card");

        // ===== 1. Класс 1: FinancialInstrument =====
        printSection("1. FinancialInstrument (class 1)");
        FinancialInstrument account = new FinancialInstrument("Ivan Ivanov", "Sberbank", "rub");
        // валюта записана как "rub" - сеттер переводит ее в "RUB"
        System.out.println(account.getInfo());
        // activate/block возвращают true, если состояние изменилось
        System.out.println("activate (already active): " + account.activate());
        System.out.println("block: " + account.block());
        System.out.println("block (already blocked): " + account.block());
        System.out.println("activate: " + account.activate());
        // println(объект) вызывает toString(), а он - getShortName()
        System.out.println("toString: " + account);

        // ===== 2. Класс 2: BankCard =====
        printSection("2. BankCard (class 2, extends FinancialInstrument)");
        BankCard card = new BankCard("Anna Petrova", "T-Bank", "RUB",
                "2200123456789012", "09/35", 50000_00);
        System.out.println(card.getInfo());
        // пополнение и две оплаты: вторая больше остатка
        report("deposit 15000.00", card.deposit(15000_00), card);
        report("pay 20000.00", card.pay(20000_00), card);
        report("pay 100000.00", card.pay(100000_00), card);
        System.out.println("masked number: " + card.getMaskedNumber());
        // заблокированной картой платить нельзя, после разблокировки - снова можно
        System.out.println("block: " + card.block());
        report("pay 100.00", card.pay(100_00), card);
        System.out.println("activate: " + card.activate());
        report("pay 100.00", card.pay(100_00), card);

        // ===== 3. Класс 3: CreditCard =====
        printSection("3. CreditCard (class 3, extends BankCard)");
        CreditCard credit = new CreditCard("Kirill Sidorov", "Alfa-Bank", "RUB",
                "5536914012345678", "12/36", 10000_00, 200000_00, 24.0);
        System.out.println(credit.getInfo());
        // своих денег хватает - кредит не нужен
        report("pay 5000.00", credit.pay(5000_00), credit);
        // своих 5000.00, недостающие 45000.00 - в долг
        report("pay 50000.00", credit.pay(50000_00), credit);
        // 24% годовых = 2% в месяц: 45000.00 * 0.02 * 3 = 2700.00
        System.out.println("interest for 3 months: " + Money.format(credit.calculateInterest(3)) + " RUB");
        // больше доступного кредита (свои 0 + кредит 155000.00) - отказ
        report("pay 300000.00", credit.pay(300000_00), credit);
        // частичное погашение, потом полное с излишком на баланс
        report("repay debt 20000.00", credit.repayDebt(20000_00), credit);
        report("repay debt 30000.00", credit.repayDebt(30000_00), credit);
        report("repay debt 1000.00", credit.repayDebt(1000_00), credit);
        System.out.println(credit.getInfo());

        // ===== 4. Полиморфизм =====
        printSection("4. Polymorphism");
        // в массиве типа FinancialInstrument лежат объекты всех трех классов;
        // getInfo() у каждого вызывается свой - Java выбирает метод по настоящему классу объекта
        FinancialInstrument[] instruments = {
                new FinancialInstrument("Pavel Kozlov", "VTB", "USD"),
                new BankCard("Maria Orlova", "Gazprombank", "RUB", "4276123412341234", "06/37", 75000_00),
                new CreditCard("Denis Novikov", "Sovcombank", "RUB", "5100987654321098", "03/38",
                        5000_00, 150000_00, 19.9)
        };
        for (FinancialInstrument item : instruments) {
            // getClass().getSimpleName() - имя настоящего класса объекта
            System.out.println("[" + item.getClass().getSimpleName() + "] " + item.getInfo());
        }
        // один и тот же вызов pay(30000.00) у дебетовой и кредитной карты с 20000.00 своих денег:
        // дебетовая отказывает, кредитная доплачивает из кредита
        BankCard[] cards = {
                new BankCard("Oleg Smirnov", "Sberbank", "RUB", "2202111122223333", "01/39", 20000_00),
                new CreditCard("Oleg Smirnov", "Sberbank", "RUB", "2202444455556666", "01/39",
                        20000_00, 50000_00, 25.0)
        };
        for (BankCard item : cards) {
            report(item.getClass().getSimpleName() + " pay 30000.00", item.pay(30000_00), item);
        }

        // ===== 5. Срок действия =====
        printSection("5. Expiry date");
        BankCard old = new BankCard("Elena Volkova", "Sberbank", "RUB", "2202777788889999", "01/20", 1000_00);
        // срок "01/20" (январь 2020) уже прошел - операции запрещены
        report("pay 100.00", old.pay(100_00), old);
        report("deposit 100.00", old.deposit(100_00), old);

        // ===== 6. Проверка неправильных значений =====
        printSection("6. Wrong values (exceptions)");
        // Каждая попытка ниже должна закончиться исключением IllegalArgumentException.
        // Как работает try/catch:
        //   - в блоке try выполняется опасный вызов;
        //   - если метод бросил исключение, остаток try пропускается и сразу выполняется catch;
        //   - если исключения не было, выполняется строка после вызова ("no error (wrong!)"),
        //     а catch пропускается. Так мы видим, что проверка в классе не сработала.
        // e.getMessage() - текст, который передали в new IllegalArgumentException("...").

        // номер карты из 15 цифр вместо 16 - ошибку бросает конструктор BankCard
        try {
            new BankCard("Test", "Bank", "RUB", "123456789012345", "01/30", 0);
            System.out.println("card number with 15 digits: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("card number with 15 digits: error: " + e.getMessage());
        }

        // месяца 13 не бывает
        try {
            card.setExpiryDate("13/30");
            System.out.println("expiry date 13/30: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("expiry date 13/30: error: " + e.getMessage());
        }

        // код валюты - ровно 3 латинские буквы
        try {
            card.setCurrency("RUBLE");
            System.out.println("currency \"RUBLE\": no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("currency \"RUBLE\": error: " + e.getMessage());
        }

        // имя из одних пробелов считается пустым
        try {
            new FinancialInstrument("  ", "Bank", "RUB");
            System.out.println("empty holder name: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("empty holder name: error: " + e.getMessage());
        }

        // баланс дебетовой карты не может быть меньше нуля
        try {
            card.setBalance(-1);
            System.out.println("negative balance: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("negative balance: error: " + e.getMessage());
        }

        // сумма оплаты должна быть больше нуля
        try {
            card.pay(-100_00);
            System.out.println("pay -100.00: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("pay -100.00: error: " + e.getMessage());
        }

        // кредитный лимит должен быть больше нуля
        try {
            credit.setCreditLimit(0);
            System.out.println("credit limit 0: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("credit limit 0: error: " + e.getMessage());
        }

        // процентная ставка - от 0 до 100
        try {
            credit.setInterestRate(150);
            System.out.println("interest rate 150: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("interest rate 150: error: " + e.getMessage());
        }

        // проценты считаются хотя бы за 1 месяц
        try {
            credit.calculateInterest(0);
            System.out.println("interest for 0 months: no error (wrong!)");
        } catch (IllegalArgumentException e) {
            System.out.println("interest for 0 months: error: " + e.getMessage());
        }

        printTitle("done");
    }

    /**
     * Напечатать результат операции и состояние карты после нее.
     * Параметр типа BankCard принимает и CreditCard (подкласс), тогда печатается еще и долг.
     */
    private static void report(String action, OperationResult result, BankCard card) {
        String line = action + " -> " + result + " | balance " + Money.format(card.getBalance());
        // instanceof - проверка "этот объект на самом деле CreditCard?"
        if (card instanceof CreditCard) {
            // приведение типа: у переменной типа BankCard нет метода getDebt(),
            // поэтому смотрим на тот же объект через переменную типа CreditCard
            CreditCard creditCard = (CreditCard) card;
            line += ", debt " + Money.format(creditCard.getDebt());
        }
        System.out.println(line);
    }

    // заголовок всей программы
    private static void printTitle(String title) {
        System.out.println("===== " + title + " =====");
    }

    // заголовок раздела, перед ним - пустая строка
    private static void printSection(String name) {
        System.out.println();
        System.out.println("--- " + name + " ---");
    }
}
