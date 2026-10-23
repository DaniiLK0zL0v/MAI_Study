import java.util.Locale;

/**
 * Класс 1 иерархии: финансовый инструмент - любое средство, которым владеет клиент банка
 * (карта, счет, вклад). Общее у всех: владелец, банк, валюта и состояние "активен / заблокирован".
 *
 * Иерархия: FinancialInstrument (1) -> BankCard (2) -> CreditCard (3).
 *
 * Собственные атрибуты: holderName, issuingBank, currency, active.
 * Собственные методы:   activate(), block(), getInfo(), getShortName().
 */
public class FinancialInstrument {

    // private - поле видно только внутри этого класса; снаружи - только через get/set (инкапсуляция)
    private String holderName;    // владелец, например "Ivan Ivanov"
    private String issuingBank;   // банк, который выпустил инструмент
    private String currency;      // код валюты из трех латинских букв: RUB, USD, EUR
    private boolean active;       // true - можно пользоваться, false - заблокирован

    /**
     * Конструктор: создает активный инструмент.
     * Значения проходят через сеттеры, поэтому проверяются так же, как при изменении.
     * Если значение неправильное, сеттер бросает IllegalArgumentException
     * и объект не создается.
     */
    public FinancialInstrument(String holderName, String issuingBank, String currency) {
        setHolderName(holderName);
        setIssuingBank(issuingBank);
        setCurrency(currency);
        // новый инструмент сразу активен
        this.active = true;
    }

    // ===== Свойства: геттеры и сеттеры с проверкой =====
    // Сеттеры объявлены final: подкласс не может их переопределить. Это важно, потому что
    // их вызывает конструктор, а метод подкласса работал бы с еще не созданной частью объекта.

    public String getHolderName() {
        return holderName;
    }

    public final void setHolderName(String holderName) {
        // null - строки нет совсем; isBlank() - пустая или только пробелы
        if (holderName == null || holderName.isBlank()) {
            throw new IllegalArgumentException("the holder name is empty");
        }
        // пробелы по краям не храним: " Ivan " -> "Ivan"
        this.holderName = holderName.strip();
    }

    public String getIssuingBank() {
        return issuingBank;
    }

    public final void setIssuingBank(String issuingBank) {
        if (issuingBank == null || issuingBank.isBlank()) {
            throw new IllegalArgumentException("the bank name is empty");
        }
        this.issuingBank = issuingBank.strip();
    }

    public String getCurrency() {
        return currency;
    }

    public final void setCurrency(String currency) {
        if (currency == null) {
            throw new IllegalArgumentException("the currency is empty");
        }
        // "rub" -> "RUB"; Locale.ROOT - чтобы результат не зависел от языка системы
        String code = currency.strip().toUpperCase(Locale.ROOT);
        // код валюты (стандарт ISO 4217) - ровно три латинские буквы
        if (!code.matches("[A-Z]{3}")) {
            throw new IllegalArgumentException("the currency must be 3 latin letters: " + currency);
        }
        this.currency = code;
    }

    // у логического поля геттер по правилам Java называется isXxx, а не getXxx
    public boolean isActive() {
        return active;
    }

    public final void setActive(boolean active) {
        this.active = active;
    }

    // ===== Собственные методы =====

    /**
     * Метод 1: активировать (разблокировать) инструмент.
     * Возвращает true, если состояние изменилось, и false, если он уже был активен.
     * Сам метод ничего не печатает - сообщение выводит тот, кто его вызвал.
     */
    public boolean activate() {
        // уже активен - менять нечего
        if (active) {
            return false;
        }
        active = true;
        return true;
    }

    /**
     * Метод 2: заблокировать инструмент. Возвращает true, если состояние изменилось,
     * и false, если он уже был заблокирован.
     */
    public boolean block() {
        // уже заблокирован - менять нечего
        if (!active) {
            return false;
        }
        active = false;
        return true;
    }

    /**
     * Метод 3: полное описание в виде текста.
     * Подклассы переопределяют его и дописывают свои поля (полиморфизм).
     */
    public String getInfo() {
        String status = active ? "active" : "blocked";
        return "holder: " + holderName + ", bank: " + issuingBank
                + ", currency: " + currency + ", status: " + status;
    }

    /**
     * Метод 4: короткое имя для сообщений, например "Sberbank/Ivan Ivanov".
     * Банковская карта переопределяет его: вместо владельца - замаскированный номер.
     */
    public String getShortName() {
        return issuingBank + "/" + holderName;
    }

    /**
     * toString() есть у любого объекта Java (метод класса Object), его вызывают
     * println(obj) и "text" + obj. Без переопределения он печатает что-то вроде
     * "FinancialInstrument@1b6d3586" - имя класса и хэш-код.
     */
    @Override
    public String toString() {
        return getShortName();
    }
}
