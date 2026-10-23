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
        // каждая попытка должна закончиться исключением IllegalArgumentException
        expectError("card number with 15 digits",
                () -> new BankCard("Test", "Bank", "RUB", "123456789012345", "01/30", 0));
        expectError("expiry date 13/30", () -> card.setExpiryDate("13/30"));
        expectError("currency \"RUBLE\"", () -> card.setCurrency("RUBLE"));
        expectError("empty holder name", () -> new FinancialInstrument("  ", "Bank", "RUB"));
        expectError("negative balance", () -> card.setBalance(-1));
        expectError("pay -100.00", () -> card.pay(-100_00));
        expectError("credit limit 0", () -> credit.setCreditLimit(0));
        expectError("interest rate 150", () -> credit.setInterestRate(150));
        expectError("interest for 0 months", () -> credit.calculateInterest(0));

        printTitle("done");
    }

    /**
     * Напечатать результат операции и состояние карты после нее.
     * Параметр типа BankCard принимает и CreditCard (подкласс), тогда печатается еще и долг.
     */
    private static void report(String action, OperationResult result, BankCard card) {
        String line = action + " -> " + result + " | balance " + Money.format(card.getBalance());
        // instanceof - проверка "этот объект - CreditCard?"; в Java 16+ сразу дает переменную нужного типа
        if (card instanceof CreditCard creditCard) {
            line += ", debt " + Money.format(creditCard.getDebt());
        }
        System.out.println(line);
    }

    /**
     * Выполнить action и убедиться, что он бросил IllegalArgumentException.
     * Runnable - стандартный интерфейс "действие без параметров"; лямбда () -> ... - его реализация.
     */
    private static void expectError(String description, Runnable action) {
        try {
            action.run();
            // сюда попадаем, только если исключения не было - это ошибка в проверках классов
            System.out.println(description + ": no error (wrong!)");
        } catch (IllegalArgumentException e) {
            // ожидаемый случай: печатаем текст исключения
            System.out.println(description + ": error: " + e.getMessage());
        }
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
